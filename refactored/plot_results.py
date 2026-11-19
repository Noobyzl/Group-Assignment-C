#!/usr/bin/env python3
"""plot_results.py - graph the results of the Solar Planner C program.

Reads the "SCENARIO SWEEP TABLE" from output/report.txt and draws:
  1. Annual generation vs. annual demand, per panel count
  2. Energy offset % (with the BUILD / BUILD SMALLER / DON'T BUILD colouring)
  3. Simple payback (years) per panel count
  4. Capex vs. annual savings per panel count

Usage
-----
  python plot_results.py                      # plot existing output/report.txt
  python plot_results.py --run BILL AREA BUDGET [PANEL]
                                             # build + run the C program first,
                                             # e.g. --run 350 60 20000 1
  python plot_results.py --save graph.png     # save instead of opening a window

Requires: pip install matplotlib
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
REPORT = os.path.join(ROOT, "output", "report.txt")
EXE = os.path.join(ROOT, "solar_planner.exe" if os.name == "nt" else "solar_planner")

SOURCES = [
    "Module_1/loader.c", "Module_1/loader_row.c", "Module_1/loader_order.c",
    "Module_1/loader_helper.c", "Module_1/panels.c",
    "Module_2/Yield.c", "Module_2/TimeStep.c", "Module_2/Fault.c", "Module_3/Logic_Decision.c",
    "Module_4/impact.c", "Module_4/sweep.c", "Module_4/tnb.c",
    "Module_5/main.c", "Module_5/input.c", "Module_5/report.c", "Module_5/report_print.c",
    "Module_5/report_csv.c", "Module_5/coverage.c", "Module_5/search.c",
]


def build_and_run(bill, area, budget, panel):
    """Compile the C program if needed and feed it the user inputs."""
    if not os.path.exists(EXE):
        cmd = ["gcc", "-std=c99",
               *SOURCES, "-o", EXE, "-lm"]
        print("Compiling:", " ".join(cmd))
        subprocess.run(cmd, cwd=ROOT, check=True)
    # bill, area, budget, panel, then "y" through the 4 report parts, "n" to skip search
    answers = f"{bill}\n{area}\n{budget}\n{panel}\ny\ny\ny\ny\nn\n"
    subprocess.run([EXE], cwd=ROOT, input=answers, text=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)


def parse_report(path):
    """Return (rows, demand_kwh). rows is a list of dicts from the sweep table."""
    if not os.path.exists(path):
        sys.exit(f"{path} not found. Run the C program first, or use --run.")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()

    m = re.search(r"Annual electricity use:\s*([\d.]+)\s*kWh", text)
    demand = float(m.group(1)) if m else None

    rows = []
    for line in text.splitlines():
        cells = [c.strip() for c in line.split("|")]
        if len(cells) != 9 or not cells[0].isdigit():
            continue
        pb = re.match(r"[\d.]+", cells[6])
        rows.append({
            "panels": int(cells[0]),
            "kwp": float(cells[1]),
            "gen": float(cells[2]),
            "offset": float(cells[3]),
            "capex": float(cells[4]),
            "saving": float(cells[5]),
            "payback": float(pb.group()) if pb else None,   # None = never pays back
            "verdict": cells[8],
        })
    if not rows:
        sys.exit("No sweep table found in the report (was the user ineligible?).")
    return rows, demand


def plot(rows, demand, save=None):
    try:
        import matplotlib
        if save:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib is required:  pip install matplotlib")

    colours = {"BUILD": "#2e9e5b", "BUILD SMALLER": "#e6a23c", "DON'T BUILD": "#d6453d"}
    n = [r["panels"] for r in rows]
    bar_cols = [colours.get(r["verdict"], "#888888") for r in rows]

    fig, ax = plt.subplots(2, 2, figsize=(13, 9))
    fig.suptitle("Solar Planner - scenario sweep results", fontsize=15, fontweight="bold")

    # 1. generation vs demand
    a = ax[0][0]
    a.bar(n, [r["gen"] for r in rows], color=bar_cols)
    if demand:
        a.axhline(demand, color="black", ls="--", label=f"Annual demand ({demand:,.0f} kWh)")
        a.legend()
    a.set_title("Annual generation")
    a.set_xlabel("Number of panels")
    a.set_ylabel("kWh / year")

    # 2. offset
    a = ax[0][1]
    a.bar(n, [r["offset"] for r in rows], color=bar_cols)
    a.set_title("Energy offset")
    a.set_xlabel("Number of panels")
    a.set_ylabel("% of demand covered")
    a.set_ylim(0, 105)

    # 3. payback
    a = ax[1][0]
    a.plot(n, [r["payback"] if r["payback"] is not None else float("nan") for r in rows],
           marker="o", color="#3b6fb6")
    for r in rows:
        if r["payback"] is None:
            a.annotate("never", (r["panels"], 0), color="red", ha="center")
    a.set_title("Simple payback")
    a.set_xlabel("Number of panels")
    a.set_ylabel("Years")
    a.grid(alpha=0.3)

    # 4. capex vs savings
    a = ax[1][1]
    a.plot(n, [r["capex"] for r in rows], marker="s", color="#8e44ad", label="Capex (RM)")
    a2 = a.twinx()
    a2.plot(n, [r["saving"] for r in rows], marker="^", color="#16a085", label="Savings (RM/year)")
    a.set_title("Cost vs. savings")
    a.set_xlabel("Number of panels")
    a.set_ylabel("Capex (RM)")
    a2.set_ylabel("Savings (RM/year)")
    h1, l1 = a.get_legend_handles_labels()
    h2, l2 = a2.get_legend_handles_labels()
    a.legend(h1 + h2, l1 + l2, loc="upper left")

    # verdict legend for the coloured bars
    from matplotlib.patches import Patch
    fig.legend(handles=[Patch(color=c, label=v) for v, c in colours.items()],
               loc="lower center", ncol=3, title="Verdict")
    fig.tight_layout(rect=(0, 0.06, 1, 0.96))

    if save:
        fig.savefig(save, dpi=150)
        print("Saved", save)
    else:
        plt.show()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--run", nargs="+", metavar=("BILL", "AREA"),
                   help="BILL AREA BUDGET [PANEL] - run the C program first")
    p.add_argument("--report", default=REPORT, help="path to report.txt")
    p.add_argument("--save", help="save the figure to this file instead of showing it")
    args = p.parse_args()

    if args.run:
        if len(args.run) not in (3, 4):
            p.error("--run needs BILL AREA BUDGET [PANEL]")
        bill, area, budget = args.run[:3]
        panel = args.run[3] if len(args.run) == 4 else "1"
        build_and_run(bill, area, budget, panel)

    rows, demand = parse_report(args.report)
    plot(rows, demand, args.save)


if __name__ == "__main__":
    main()
