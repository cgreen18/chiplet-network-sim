#!/usr/bin/env python3
"""Run ChipletNetworkSim sweeps and write injection-rate vs latency CSVs."""

import argparse
import csv
import re
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parent
SIM_BIN = REPO_ROOT / "builds" / "Release_v3" / "ChipletNetworkSim"
INPUT_DIR = REPO_ROOT / "input"
DEFAULT_TOPOS = ("kite_large", "dbl_bfly_x")

INJ_RATE_RE = re.compile(
    r"Injection rate:\s*([\d.]+)\s+flits/\(node\*cycle\)"
)
AVG_LATENCY_RE = re.compile(r"Average latency:\s*([\d.]+)")


def parse_sim_output(text):
    # type: (str) -> List[Tuple[float, float]]
    """Extract (injection_rate, avg_latency) pairs from simulator stdout."""
    rows = []  # type: List[Tuple[float, float]]
    inj_rate = None  # type: Optional[float]

    for line in text.splitlines():
        inj_match = INJ_RATE_RE.search(line)
        if inj_match:
            inj_rate = float(inj_match.group(1))
            continue

        if inj_rate is not None:
            lat_match = AVG_LATENCY_RE.search(line)
            if lat_match:
                rows.append((inj_rate, float(lat_match.group(1))))
                inj_rate = None

    return rows


def run_topology(topo):
    # type: (str) -> List[Tuple[float, float]]
    ini_path = INPUT_DIR / f"{topo}.ini"
    if not ini_path.is_file():
        raise FileNotFoundError(f"missing config: {ini_path}")

    print(f"Running {SIM_BIN.name} {ini_path.relative_to(REPO_ROOT)} ...", flush=True)
    proc = subprocess.run(
        [str(SIM_BIN), str(ini_path)],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    output = proc.stdout
    if proc.stderr:
        sys.stderr.write(proc.stderr)

    if proc.returncode != 0:
        raise RuntimeError(
            f"{topo}: simulator exited with code {proc.returncode}"
        )

    rows = parse_sim_output(output)
    if not rows:
        raise RuntimeError(f"{topo}: no injection-rate / latency pairs found in output")
    return rows


def write_csv(topo, rows, out_dir):
    # type: (str, List[Tuple[float, float]], Path) -> Path
    out_path = out_dir / f"{topo}_injrates_lats.csv"
    with out_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["injection_rate", "avg_latency"])
        for inj_rate, latency in rows:
            writer.writerow([inj_rate, latency])
    return out_path


def main():
    parser = argparse.ArgumentParser(
        description="Run ChipletNetworkSim and save injection-rate vs latency CSVs."
    )
    parser.add_argument(
        "topologies",
        nargs="*",
        default=list(DEFAULT_TOPOS),
        help=f"topology names (default: {' '.join(DEFAULT_TOPOS)})",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=REPO_ROOT,
        help="directory for output CSV files (default: repo root)",
    )
    args = parser.parse_args()

    if not SIM_BIN.is_file():
        sys.exit(f"simulator not found: {SIM_BIN}")

    args.output_dir.mkdir(parents=True, exist_ok=True)

    for topo in args.topologies:
        rows = run_topology(topo)
        out_path = write_csv(topo, rows, args.output_dir)
        print(f"Wrote {len(rows)} rows to {out_path}")


if __name__ == "__main__":
    main()
