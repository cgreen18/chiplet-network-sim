#!/usr/bin/env python3
import csv
import sys
import math

import matplotlib.pyplot as plt

# --- plot configuration ---
DIVIDE_INJECTION_RATE_BY_20 = True

X_LIM = (0, 1.8)
Y_LIM = (0, 80)

# Injection-rate x positions in plot coordinates (not divided by 20).
# Keys must match topology names used when plotting curves below.
VERTICAL_LINES = {
    "kite_large": {
        "Bi. B/W Bound": 0.08,
        "SC": 0.08,
        "MCF": 0.08,
        "Single Path": 0.07692307692307693,
    },
    "dbl_bfly_x": {
        "Bi. B/W Bound": 0.08,
        "SC": 0.0625,
        "MCF": 0.0597014925373134,
        "Single Path": 0.058823529411764705,
    },
}
# -------------------------


def load_csv(path, latency_field_candidates):
    """Load (injection_rate, latency) from a CSV.

    latency_field_candidates: list of possible column names for latency.
    """
    injection_rates = []
    latencies = []

    with open(path, "r", newline="") as f:
        reader = csv.DictReader(f)
        header_fields = reader.fieldnames or []

        latency_field = None
        for cand in latency_field_candidates:
            if cand in header_fields:
                latency_field = cand
                break
        if latency_field is None:
            raise ValueError(
                "No latency field found in {0}. Tried: {1}. Found: {2}".format(
                    path, latency_field_candidates, header_fields
                )
            )

        for row in reader:
            r_str = (row.get("injection_rate") or "").strip()
            lat_str = (row.get(latency_field) or "").strip()
            if not r_str or not lat_str:
                continue

            try:
                r = float(r_str)
                lat = float(lat_str)
            except ValueError:
                continue
            if math.isnan(lat):
                continue

            injection_rates.append(r)
            latencies.append(lat)

    if not injection_rates:
        raise ValueError("No valid data in {0}".format(path))

    paired = sorted(zip(injection_rates, latencies), key=lambda x: x[0])
    return list(zip(*paired))


def scale_injection_rates(rates):
  # type: (list) -> list
    if DIVIDE_INJECTION_RATE_BY_20:
        return [r / 20.0 for r in rates]
    return rates


def scale_x(x):
    if DIVIDE_INJECTION_RATE_BY_20:
        return x / 20.0
    return x


def main():
    # Usage:
    #   python3 plot_four_files.py [kite_large_csv] [dbl_bfly_x_csv]

    kite_csv = sys.argv[1] if len(sys.argv) > 1 else "kite_large_injrates_lats.csv"
    bfly_csv = sys.argv[2] if len(sys.argv) > 2 else "dbl_bfly_x_injrates_lats.csv"

    latency_fields = [
        "avg_latency",
        "avg_packet_latency_cycles",
        "avg_packet_latency",
    ]

    kite_r, kite_lat = load_csv(kite_csv, latency_fields)
    bfly_r, bfly_lat = load_csv(bfly_csv, latency_fields)

    kite_r = scale_injection_rates(list(kite_r))
    bfly_r = scale_injection_rates(list(bfly_r))

    plt.figure(figsize=(8, 4))

    fs_a = 14
    fs_b = 12

    topo_lines = [
        ("dbl_bfly_x", bfly_r, bfly_lat, "o", "Double Butterfly X"),
        ("kite_large", kite_r, kite_lat, "o", "Kite Large"),
    ]
    topo_colors = {}
    for topo_key, x_vals, y_vals, marker, legend_label in topo_lines:
        line, = plt.plot(
            x_vals,
            y_vals,
            marker=marker,
            linestyle="-",
            label=legend_label,
        )
        topo_colors[topo_key] = line.get_color()

    plt.xlabel("Injection rate (flits/node/cycle)", fontsize=fs_a)
    plt.ylabel("Avg. pkt. latency (cycles)", fontsize=fs_a)
    plt.grid(True)
    plt.legend(fontsize=fs_b)

    plt.xlim([scale_x(X_LIM[0]), scale_x(X_LIM[1])])
    plt.ylim(list(Y_LIM))

    plt.yticks([10 * i for i in range(Y_LIM[1] // 10 + 1)], fontsize=fs_b)
    plt.xticks(fontsize=fs_b)

    for topo_key, metrics in VERTICAL_LINES.items():
        if topo_key not in topo_colors:
            raise ValueError(
                "VERTICAL_LINES topology {0!r} has no plotted curve. "
                "Expected one of: {1}".format(topo_key, list(topo_colors.keys()))
            )
        color = topo_colors[topo_key]
        for _metric_name, x in metrics.items():
            plt.axvline(
                x=x,
                linestyle="--",
                linewidth=2,
                color=color,
            )

    out_png = "cnsim_latency_vs_load.png"
    plt.savefig(out_png, bbox_inches="tight", dpi=1200)
    print("Plot saved to {0}".format(out_png))

    try:
        plt.show()
    except Exception:
        pass


if __name__ == "__main__":
    main()
