#!/usr/bin/env python3
"""Generate netrace configs, run ChipletNetworkSim on Slurm, poll jobs, write CSV."""

import argparse
import csv
import math
import re
import subprocess
import sys
import time
from enum import Enum
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent
SIM_BIN = REPO_ROOT / "builds" / "Release" / "ChipletNetworkSim"
TEMPLATE_INI = REPO_ROOT / "input" / "netrace" / "twenty_node_template.ini"
NETRACE_INI_DIR = REPO_ROOT / "input" / "netrace"
PARSEC_TRACE_DIR = REPO_ROOT / "input" / "parsec_traces"
OUTPUT_DIR = REPO_ROOT / "output"
DEFAULT_TOPOS = ("kite_large", "dbl_bfly_x")
DEFAULT_SLURM_ACCOUNT = "mithuna"
DEFAULT_SLURM_TIME = "1-00:00:00"
DEFAULT_SLURM_CPUS = "32"
DEFAULT_SLURM_PARTITION = "cpu"
DEFAULT_NUM_REGIONS = 5

TRACE_RE = re.compile(r"^(.+)_64c_(.+)\.tra\.bz2$")
AVG_LATENCY_RE = re.compile(r"Average latency:\s*([\d.]+)")
REGION_MARK_RE = re.compile(r"Netrace region (\d+):")

TERMINAL_SLURM_STATES = frozenset(
    [
        "COMPLETED",
        "FAILED",
        "CANCELLED",
        "TIMEOUT",
        "NODE_FAIL",
        "PREEMPTED",
        "OUT_OF_MEMORY",
        "BOOT_FAIL",
        "DEADLINE",
    ]
)


class JobPhase(Enum):
    PENDING_SUBMIT = "pending_submit"
    QUEUED = "queued"
    RUNNING = "running"
    PARSING = "parsing"
    DONE = "done"
    FAILED = "failed"


class SweepCase(object):
    def __init__(self, topo, benchmark, size):
        self.topo = topo
        self.benchmark = benchmark
        self.size = size

    @property
    def run_name(self):
        return "{0}_{1}_{2}".format(self.topo, self.benchmark, self.size)

    @property
    def trace_label(self):
        return "{0}_{1}".format(self.benchmark, self.size)

    @property
    def ini_path(self):
        return NETRACE_INI_DIR / "{0}.ini".format(self.run_name)

    @property
    def output_path(self):
        return OUTPUT_DIR / "{0}.txt".format(self.run_name)


class JobRecord(object):
    def __init__(self, case):
        self.case = case
        self.job_id = None
        self.phase = JobPhase.PENDING_SUBMIT
        self.slurm_state = ""
        self.region_latencies = None
        self.geomean = None
        self.error = ""


def discover_traces(trace_dir):
    if not trace_dir.is_dir():
        raise FileNotFoundError("trace directory not found: {0}".format(trace_dir))

    traces = []
    for path in sorted(trace_dir.glob("*.tra.bz2")):
        match = TRACE_RE.match(path.name)
        if not match:
            print(
                "warning: skipping unrecognized trace file {0}".format(path.name),
                file=sys.stderr,
            )
            continue
        traces.append((match.group(1), match.group(2)))
    if not traces:
        raise RuntimeError(
            "no traces matching *_64c_*.tra.bz2 in {0}".format(trace_dir)
        )
    return traces


def build_cases(topos, traces):
    cases = []
    for topo in topos:
        for benchmark, size in traces:
            cases.append(SweepCase(topo, benchmark, size))
    return cases


def load_completed_keys(csv_path):
    if not csv_path.is_file():
        return set()
    done = set()
    with csv_path.open(newline="") as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 3 or row[0] == "topo":
                continue
            done.add((row[0], row[1]))
    return done


def generate_ini(case, template_text, force):
    if case.ini_path.is_file() and not force:
        return
    content = (
        template_text.replace("<topo>", case.topo)
        .replace("<benchmark>", case.benchmark)
        .replace("<size>", case.size)
    )
    case.ini_path.write_text(content)


def geomean(values):
    if not values:
        raise ValueError("geomean requires at least one value")
    if any(v <= 0 for v in values):
        raise ValueError("geomean requires positive latencies, got {0}".format(values))
    return math.exp(sum(math.log(v) for v in values) / float(len(values)))


def parse_region_latencies(output_path, num_regions):
    text = output_path.read_text(errors="replace")
    by_region = {}
    parts = REGION_MARK_RE.split(text)
    if len(parts) >= 3:
        idx = 1
        while idx + 1 < len(parts):
            region_id = int(parts[idx])
            section = parts[idx + 1]
            matches = AVG_LATENCY_RE.findall(section)
            if not matches:
                raise ValueError(
                    "no 'Average latency:' for region {0} in {1}".format(
                        region_id, output_path
                    )
                )
            by_region[region_id] = float(matches[-1])
            idx += 2
    else:
        matches = AVG_LATENCY_RE.findall(text)
        if len(matches) < num_regions:
            raise ValueError(
                "expected at least {0} 'Average latency:' lines in {1}, found {2}".format(
                    num_regions, output_path, len(matches)
                )
            )
        for region_id in range(num_regions):
            by_region[region_id] = float(matches[-num_regions + region_id])

    latencies = []
    for region_id in range(num_regions):
        if region_id not in by_region:
            raise ValueError(
                "missing latency for region {0} in {1} (found regions {2})".format(
                    region_id, output_path, sorted(by_region.keys())
                )
            )
        latencies.append(by_region[region_id])
    return latencies


def slurm_query_states(job_ids):
    ids = [jid for jid in job_ids if jid]
    if not ids:
        return {}

    proc = subprocess.run(
        ["sacct", "-j", ",".join(ids), "-n", "-X", "-o", "JobID,State"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        check=False,
    )
    states = {}
    if proc.returncode == 0:
        for line in proc.stdout.splitlines():
            parts = line.split()
            if len(parts) < 2:
                continue
            job_id, state = parts[0], parts[1]
            if "." in job_id:
                continue
            states[job_id] = state
        if states:
            return states

    proc = subprocess.run(
        ["squeue", "-j", ",".join(ids), "-h", "-o", "%i %T"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        check=False,
    )
    if proc.returncode != 0:
        return states
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            states[parts[0]] = parts[1]
    return states


def submit_slurm_job(case, account, time_limit, cpus, partition, slurm_log_dir):
    if not SIM_BIN.is_file():
        raise FileNotFoundError("simulator not found: {0}".format(SIM_BIN))

    slurm_log_dir.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    sim_cmd = (
        "cd {repo} && {sim} {ini} | tee {out}".format(
            repo=REPO_ROOT,
            sim=SIM_BIN,
            ini=case.ini_path.relative_to(REPO_ROOT),
            out=case.output_path.relative_to(REPO_ROOT),
        )
    )
    proc = subprocess.run(
        [
            "sbatch",
            "--parsable",
            "--account={0}".format(account),
            "-t{0}".format(time_limit),
            "--cpus-per-task={0}".format(cpus),
            "-p{0}".format(partition),
            "--job-name={0}".format(case.run_name[:100]),
            "--output={0}".format(slurm_log_dir / (case.run_name + ".slurm.out")),
            "--error={0}".format(slurm_log_dir / (case.run_name + ".slurm.err")),
            "--wrap={0}".format(sim_cmd),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        check=False,
    )
    if proc.returncode != 0:
        msg = proc.stderr.strip() or proc.stdout.strip()
        raise RuntimeError("sbatch failed for {0}: {1}".format(case.run_name, msg))
    job_id = proc.stdout.strip().split(";")[0]
    if not job_id:
        raise RuntimeError("sbatch returned no job id for {0}".format(case.run_name))
    return job_id


def csv_header(num_regions):
    row = ["topo", "trace"]
    row.extend(["latency_r{0}".format(r) for r in range(num_regions)])
    row.append("geomean")
    return row


def append_csv_row(csv_path, case, region_latencies, geomean, num_regions):
    write_header = not csv_path.is_file() or csv_path.stat().st_size == 0
    with csv_path.open("a", newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(csv_header(num_regions))
        row = [case.topo, case.trace_label]
        row.extend(["{0:g}".format(v) for v in region_latencies])
        row.append("{0:g}".format(geomean))
        writer.writerow(row)


# Fixed-width columns for poll progress output.
STATUS_RUN_NAME_W = 42
STATUS_JOB_ID_W = 10
STATUS_PHASE_W = 16
STATUS_SLURM_W = 14
STATUS_LATENCY_W = 12
STATUS_ERROR_W = 40


def _fit_field(text, width):
    if text is None:
        text = ""
    text = str(text)
    if len(text) <= width:
        return text
    if width <= 3:
        return text[:width]
    return text[: width - 3] + "..."


def status_header_line():
    return "{run:<{run_w}} {job:>{job_w}} {phase:<{phase_w}} {slurm:<{slurm_w}} {lat:>{lat_w}} {err:<{err_w}}".format(
        run="run_name",
        job="job_id",
        phase="phase",
        slurm="slurm",
        lat="geomean",
        err="error",
        run_w=STATUS_RUN_NAME_W,
        job_w=STATUS_JOB_ID_W,
        phase_w=STATUS_PHASE_W,
        slurm_w=STATUS_SLURM_W,
        lat_w=STATUS_LATENCY_W,
        err_w=STATUS_ERROR_W,
    )


def status_line(record):
    case = record.case
    job_id = record.job_id if record.job_id else "-"
    slurm = record.slurm_state if record.slurm_state else "-"
    if record.geomean is not None:
        latency = "{0:g}".format(record.geomean)
    else:
        latency = "-"
    error = record.error if record.error else "-"
    return "{run:<{run_w}} {job:>{job_w}} {phase:<{phase_w}} {slurm:<{slurm_w}} {lat:>{lat_w}} {err:<{err_w}}".format(
        run=_fit_field(case.run_name, STATUS_RUN_NAME_W),
        job=_fit_field(job_id, STATUS_JOB_ID_W),
        phase=_fit_field(record.phase.value, STATUS_PHASE_W),
        slurm=_fit_field(slurm, STATUS_SLURM_W),
        lat=_fit_field(latency, STATUS_LATENCY_W),
        err=_fit_field(error, STATUS_ERROR_W),
        run_w=STATUS_RUN_NAME_W,
        job_w=STATUS_JOB_ID_W,
        phase_w=STATUS_PHASE_W,
        slurm_w=STATUS_SLURM_W,
        lat_w=STATUS_LATENCY_W,
        err_w=STATUS_ERROR_W,
    )


def print_status(records, show_header=False):
    if show_header:
        print(status_header_line())
        print("-" * (
            STATUS_RUN_NAME_W
            + STATUS_JOB_ID_W
            + STATUS_PHASE_W
            + STATUS_SLURM_W
            + STATUS_LATENCY_W
            + STATUS_ERROR_W
            + 5
        ))
    for record in records:
        print(status_line(record))
    sys.stdout.flush()


def poll_and_collect(records, csv_path, poll_interval, num_regions):
    header_printed = False
    while True:
        active = [
            r for r in records if r.phase not in (JobPhase.DONE, JobPhase.FAILED)
        ]
        if not active:
            break

        states = slurm_query_states([r.job_id for r in active if r.job_id])
        for record in active:
            if record.phase == JobPhase.PENDING_SUBMIT:
                continue

            state = states.get(record.job_id or "", "")
            if state:
                record.slurm_state = state

            if record.phase in (JobPhase.QUEUED, JobPhase.RUNNING):
                if state in ("PENDING", "CONFIGURING", "RESV_DEL_HOLD", "REQUEUED"):
                    record.phase = JobPhase.QUEUED
                elif state == "RUNNING":
                    record.phase = JobPhase.RUNNING
                elif state in TERMINAL_SLURM_STATES:
                    if state == "COMPLETED":
                        record.phase = JobPhase.PARSING
                    else:
                        record.phase = JobPhase.FAILED
                        record.error = "slurm state {0}".format(state)
                elif state == "" and record.job_id:
                    if record.case.output_path.is_file():
                        record.phase = JobPhase.PARSING

            if record.phase == JobPhase.PARSING:
                try:
                    if not record.case.output_path.is_file():
                        raise FileNotFoundError(
                            "missing output file {0}".format(record.case.output_path)
                        )
                    region_latencies = parse_region_latencies(
                        record.case.output_path, num_regions
                    )
                    gmean = geomean(region_latencies)
                    append_csv_row(
                        csv_path,
                        record.case,
                        region_latencies,
                        gmean,
                        num_regions,
                    )
                    record.region_latencies = region_latencies
                    record.geomean = gmean
                    record.phase = JobPhase.DONE
                    record.error = ""
                except Exception as exc:
                    record.phase = JobPhase.FAILED
                    record.error = str(exc)

        print_status(records, show_header=not header_printed)
        header_printed = True
        if all(r.phase in (JobPhase.DONE, JobPhase.FAILED) for r in records):
            break
        time.sleep(poll_interval)


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Generate netrace INI files, submit ChipletNetworkSim runs on Slurm, "
            "poll to completion, and append average latency to a CSV."
        )
    )
    parser.add_argument(
        "--topos",
        nargs="+",
        default=list(DEFAULT_TOPOS),
        help="topologies to sweep (default: {0})".format(" ".join(DEFAULT_TOPOS)),
    )
    parser.add_argument(
        "--trace-dir",
        type=Path,
        default=PARSEC_TRACE_DIR,
        help="directory containing <benchmark>_64c_<size>.tra.bz2 traces",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        default=REPO_ROOT / "netrace_slurm_results.csv",
        help="output CSV path (default: netrace_slurm_results.csv in repo root)",
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=60.0,
        help="seconds between Slurm status polls (default: 60)",
    )
    parser.add_argument(
        "--force-ini",
        action="store_true",
        help="regenerate INI files even if they already exist",
    )
    parser.add_argument(
        "--skip-existing",
        action="store_true",
        default=True,
        help="skip cases already present in the CSV (default: enabled)",
    )
    parser.add_argument(
        "--no-skip-existing",
        action="store_false",
        dest="skip_existing",
        help="re-run cases even if they are already in the CSV",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="generate INI files and print planned jobs without submitting to Slurm",
    )
    parser.add_argument(
        "--account",
        default=DEFAULT_SLURM_ACCOUNT,
        help="Slurm account (default: {0})".format(DEFAULT_SLURM_ACCOUNT),
    )
    parser.add_argument(
        "--time",
        default=DEFAULT_SLURM_TIME,
        help="Slurm time limit (default: {0})".format(DEFAULT_SLURM_TIME),
    )
    parser.add_argument(
        "--cpus-per-task",
        default=DEFAULT_SLURM_CPUS,
        help="Slurm cpus-per-task (default: {0})".format(DEFAULT_SLURM_CPUS),
    )
    parser.add_argument(
        "-p",
        "--partition",
        default=DEFAULT_SLURM_PARTITION,
        help="Slurm partition (default: {0})".format(DEFAULT_SLURM_PARTITION),
    )
    parser.add_argument(
        "--num-regions",
        type=int,
        default=DEFAULT_NUM_REGIONS,
        help="number of netrace regions per run (default: {0})".format(
            DEFAULT_NUM_REGIONS
        ),
    )
    args = parser.parse_args()

    if args.num_regions < 1:
        sys.exit("--num-regions must be at least 1")

    if not TEMPLATE_INI.is_file():
        sys.exit("template not found: {0}".format(TEMPLATE_INI))

    traces = discover_traces(args.trace_dir)
    cases = build_cases(args.topos, traces)
    template_text = TEMPLATE_INI.read_text()
    slurm_log_dir = OUTPUT_DIR / "slurm_logs"

    completed = load_completed_keys(args.csv) if args.skip_existing else set()
    records = []
    for case in cases:
        key = (case.topo, case.trace_label)
        if key in completed:
            print("skip (already in CSV): {0}".format(case.run_name))
            sys.stdout.flush()
            records.append(JobRecord(case))
            records[-1].phase = JobPhase.DONE
            continue
        generate_ini(case, template_text, force=args.force_ini)
        records.append(JobRecord(case))

    pending = [r for r in records if r.phase == JobPhase.PENDING_SUBMIT]
    if args.dry_run:
        for record in pending:
            print(
                "would submit: {0} ini={1} out={2}".format(
                    record.case.run_name,
                    record.case.ini_path.relative_to(REPO_ROOT),
                    record.case.output_path.relative_to(REPO_ROOT),
                )
            )
            sys.stdout.flush()
        print("planned {0} job(s); dry-run, not submitting.".format(len(pending)))
        sys.stdout.flush()
        return 0

    for record in pending:
        try:
            job_id = submit_slurm_job(
                record.case,
                account=args.account,
                time_limit=args.time,
                cpus=args.cpus_per_task,
                partition=args.partition,
                slurm_log_dir=slurm_log_dir,
            )
            record.job_id = job_id
            record.phase = JobPhase.QUEUED
            print(
                "submitted {0} job_id={1}".format(record.case.run_name, job_id)
            )
            sys.stdout.flush()
        except Exception as exc:
            record.phase = JobPhase.FAILED
            record.error = str(exc)

    poll_and_collect(records, args.csv, args.poll_interval, args.num_regions)

    done = sum(1 for r in records if r.phase == JobPhase.DONE)
    failed = sum(1 for r in records if r.phase == JobPhase.FAILED)
    print(
        "finished: {0} succeeded, {1} failed; CSV={2}".format(
            done, failed, args.csv
        )
    )
    sys.stdout.flush()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
