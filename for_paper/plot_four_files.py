#!/usr/bin/env python3
import csv
import sys
import math

import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

# --- plot configuration ---
DIVIDE_INJECTION_RATE_BY_20 = True

X_LIM = (0, 1.8)
Y_LIM = (0, 85)
Y_TICK_INTERVAL = 20

COMMON_XLABEL = True
X_LABEL = "Injection Rate (flits/cycle/node)"
COMMON_XLABEL_Y = 0.0  # figure y position for shared x-axis label

COMMON_YLABEL = True
Y_LABEL = "Average Pkt. Latency (cycles)"
COMMON_YLABEL_X = 0.02  # figure x position for shared y-axis label

LEGEND_NCOL = 3
SIMULATION_LEGEND_LABEL = "Simulation"
LEGEND_BBOX_Y = 0.9  # figure y anchor for legend above plots
LEGEND_COLUMNSPACING = 0.6  # horizontal gap between legend columns
LEGEND_HANDTEXT_PAD = 0.4  # horizontal gap between handle and label

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


def combined_legend_handles():
    # type: () -> list
    simulation = Line2D(
        [0],
        [0],
        color="black",
        linestyle="-",
        marker="o",
        markersize=4,
        linewidth=2,
        label=SIMULATION_LEGEND_LABEL,
    )
    return [simulation] + vline_legend_handles()


def style_axes(ax, fs_b, show_xticklabels=True):
    ax.set_xlim([scale_x(X_LIM[0]), scale_x(X_LIM[1])])
    ax.set_ylim(list(Y_LIM))
    ax.set_xlabel("")
    ax.set_ylabel("")
    ax.grid(True)
    ax.set_yticks(
        [Y_TICK_INTERVAL * i for i in range(Y_LIM[1] // Y_TICK_INTERVAL + 1)]
    )
    ax.tick_params(axis="y", labelsize=fs_b)
    if show_xticklabels:
        ax.tick_params(axis="x", labelsize=fs_b)
    else:
        ax.tick_params(axis="x", labelbottom=False, labelsize=fs_b)


def add_inset_title(ax, title, fs_a):
    ax.text(
        0.03,
        0.97,
        title,
        transform=ax.transAxes,
        fontsize=fs_a-1,
        va="top",
        ha="left",
    )


def set_common_xlabel(fig, fs_a):
    if not COMMON_XLABEL:
        return
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


def set_common_ylabel(fig, fs_a):
    if not COMMON_YLABEL:
        return
    if hasattr(fig, "supylabel"):
        fig.supylabel(Y_LABEL, fontsize=fs_a, x=COMMON_YLABEL_X)
    else:
        fig.text(
            COMMON_YLABEL_X,
            0.5,
            Y_LABEL,
            ha="center",
            va="center",
            rotation=90,
            fontsize=fs_a,
        )


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

    fig, axes = plt.subplots(2, 1, figsize=(4, 3.5), sharex=True, sharey=True)
    axes = axes.flatten()

    subplots = [
        (axes[0], "dbl_bfly_x", bfly_r, bfly_lat, "Double Butterfly X"),
        (axes[1], "kite_large", kite_r, kite_lat, "Kite Large"),
    ]

    for i, (ax, topo_key, x_vals, y_vals, title) in enumerate(subplots):
        curve_line, = ax.plot(
            x_vals,
            y_vals,
            marker="o",
            linestyle="-",
            markersize=4,
        )
        plot_vertical_lines(ax, topo_key, curve_line.get_color())
        show_xticklabels = i == len(subplots) - 1
        style_axes(ax, fs_b, show_xticklabels=show_xticklabels)
        add_inset_title(ax, title, fs_a)

    fig.legend(
        handles=combined_legend_handles(),
        loc="lower center",
        bbox_to_anchor=(0.5, LEGEND_BBOX_Y),
        ncol=LEGEND_NCOL,
        fontsize=fs_b,
        frameon=True,
        columnspacing=LEGEND_COLUMNSPACING,
        handletextpad=LEGEND_HANDTEXT_PAD,
    )

    fig.tight_layout()
    fig.subplots_adjust(left=0.16, bottom=0.14, top=0.88, hspace=0.08)

    set_common_xlabel(fig, fs_a)
    set_common_ylabel(fig, fs_a)

    out_png = "cnsim_latency_vs_load.png"
    fig.savefig(out_png, bbox_inches="tight", dpi=1200)
    print("Plot saved to {0}".format(out_png))

    try:
        plt.show()
    except Exception:
        pass


if __name__ == "__main__":
    main()
