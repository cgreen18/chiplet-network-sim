#!/usr/bin/env python3
import csv
import sys
import math

import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

# --- plot configuration ---
DIVIDE_INJECTION_RATE_BY_20 = True

X_LIM = (0, 1.8)
Y_LIM = (0, 80)

COMMON_XLABEL = True
X_LABEL = "Injection Rate (flits/cycle/node)"
COMMON_XLABEL_Y = -0.015  # figure y position for shared x-axis label

Y_LABEL = "Average Packet\nLatency (cycles)"

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

# Linestyle for each vertical-line metric (shared across subplots).
VLINE_LINESTYLES = {
    "Bi. B/W Bound": "-",
    "SC": "--",
    "MCF": "-.",
    "Single Path": ":",
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


def plot_vertical_lines(ax, topo_key, curve_color):
    if topo_key not in VERTICAL_LINES:
        raise ValueError(
            "No VERTICAL_LINES entry for topology {0!r}".format(topo_key)
        )
    for metric_name, x in VERTICAL_LINES[topo_key].items():
        if metric_name not in VLINE_LINESTYLES:
            raise ValueError(
                "No VLINE_LINESTYLES entry for metric {0!r}".format(metric_name)
            )
        ax.axvline(
            x=x,
            linestyle=VLINE_LINESTYLES[metric_name],
            linewidth=2,
            color=curve_color,
        )


def vline_legend_handles():
    # type: () -> list
    return [
        Line2D(
            [0],
            [0],
            color="black",
            linestyle=VLINE_LINESTYLES[name],
            linewidth=2,
            label=name,
        )
        for name in VLINE_LINESTYLES
    ]


def style_axes(ax, fs_a, fs_b, show_ylabel=True, show_xlabel=not COMMON_XLABEL):
    ax.set_xlim([scale_x(X_LIM[0]), scale_x(X_LIM[1])])
    ax.set_ylim(list(Y_LIM))
    if show_xlabel:
        ax.set_xlabel(X_LABEL, fontsize=fs_a)
    else:
        ax.set_xlabel("")
    if show_ylabel:
        ax.set_ylabel(Y_LABEL, fontsize=fs_a)
    else:
        ax.set_ylabel("")
    ax.grid(True)
    Y_TICK_INTERVAL = 20
    ax.set_yticks([Y_TICK_INTERVAL * i for i in range(Y_LIM[1] // Y_TICK_INTERVAL + 1)])
    ax.tick_params(axis="both", labelsize=fs_b)


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

    fs_a = 14
    fs_b = 12

    fig, axes = plt.subplots(1, 2, figsize=(6.5, 1.75), sharey=True)

    subplots = [
        (axes[0], "dbl_bfly_x", bfly_r, bfly_lat, "Double Butterfly X"),
        (axes[1], "kite_large", kite_r, kite_lat, "Kite Large"),
    ]

    for ax, topo_key, x_vals, y_vals, title in subplots:
        curve_line, = ax.plot(
            x_vals,
            y_vals,
            marker="o",
            linestyle="-",
            markersize=4,
        )
        plot_vertical_lines(ax, topo_key, curve_line.get_color())
        show_ylabel = topo_key != "kite_large"
        style_axes(ax, fs_a, fs_b, show_ylabel=show_ylabel)
        ax.set_title(title, fontsize=fs_a)

    fig.legend(
        handles=vline_legend_handles(),
        loc="lower center",
        bbox_to_anchor=(0.5, -0.25),
        ncol=len(VLINE_LINESTYLES),
        fontsize=fs_b,
        frameon=True,
    )

    fig.tight_layout()
    fig.subplots_adjust(bottom=0.26)

    if COMMON_XLABEL:
        if hasattr(fig, "supxlabel"):
            fig.supxlabel(X_LABEL, fontsize=fs_a, y=COMMON_XLABEL_Y)
        else:
            fig.text(
                0.5,
                COMMON_XLABEL_Y,
                X_LABEL,
                ha="center",
                va="center",
                fontsize=fs_a,
            )

    out_png = "cnsim_latency_vs_load.png"
    fig.savefig(out_png, bbox_inches="tight", dpi=1200)
    print("Plot saved to {0}".format(out_png))

    try:
        plt.show()
    except Exception:
        pass


if __name__ == "__main__":
    main()
