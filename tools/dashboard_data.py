#!/usr/bin/env python3
"""Regenerate the dashboard datasets (dashboard/data/*.csv) from the built binaries.

    cmake -S . -B build && cmake --build build -j
    python3 tools/dashboard_data.py [--out dashboard/data] [--build build]

equity, depth and seeds are deterministic. latency/throughput are medians of five runs on whatever machine
runs this, so they will differ from the committed copy. research.csv is parsed from the saved report
(docs/research_report_synthetic.md); re-run tools/research.py first if the model or data changed.
transport.csv records the BlazingMQ verification runs documented in docs/blazingmq.md; those need a local
broker (see that file) and are not re-run here.
"""
import argparse, csv, os, re, statistics, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write(path, header, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")  # LF only: some CSV readers keep a trailing \r otherwise
        w.writerow(header)
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "dashboard", "data"))
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    exe = lambda n: os.path.join(a.build, n)
    run = lambda *c: subprocess.run(c, capture_output=True, text=True, check=True, cwd=ROOT).stdout

    # equity curves (1M messages, latency 10, seed 42) and book depth snapshots
    run(exe("backtest"), "1000000", "10", "42", os.path.join(a.out, "equity.csv"))
    run(exe("dump_depth"), "1000000", "100", os.path.join(a.out, "depth.csv"), "10", "42")

    # order book benchmark: five runs, keep the run with the median throughput
    runs = []
    for _ in range(5):
        b = run(exe("lob_bench"))
        runs.append((float(re.search(r"throughput:\s*([\d.]+) M ops/s", b).group(1)),
                     dict(re.findall(r"(p50|p90|p99|p99\.9)=(\d+)", b))))
    runs.sort(key=lambda r: r[0])
    ops, lat = runs[2]
    write(os.path.join(a.out, "latency.csv"), ["percentile", "ns"], [[k, lat[k]] for k in ("p50", "p90", "p99", "p99.9")])

    pipe, dups, rtx = [], 0, 0
    for _ in range(5):
        p = run(exe("pipeline"), "1000000")
        pipe.append(float(re.search(r"throughput ([\d.]+) M msgs/s", p).group(1)))
        dups = int(re.search(r"dup-dropped (\d+)", p).group(1)); rtx = int(re.search(r"retransmits (\d+)", p).group(1))
    write(os.path.join(a.out, "throughput.csv"), ["stage", "million_per_s", "unit", "note"], [
        ["Order book", ops, "ops", "2M mixed ops, in process (median of 5 runs)"],
        ["Feed handler", statistics.median(pipe), "msgs", "1M msgs, 2% loss, 1% dup, reordering (median of 5 runs)"],
        ["BlazingMQ round trip", 0.024, "msgs", "500k msgs, debug-built broker, shared 4 vCPU (single run, see docs/blazingmq.md)"]])
    write(os.path.join(a.out, "pipeline.csv"), ["messages", "duplicates_dropped", "retransmits"], [[1000000, dups, rtx]])

    # multi-seed backtests
    rows = []
    for seed in range(1, 6):
        for line in run(exe("backtest"), "300000", "10", str(seed)).splitlines()[1:]:
            m = re.match(r"([a-z-]+)\s+net=\s*([-+\d.]+) gross=\s*([-+\d.]+) fees=\s*([\d.]+) fills=\s*(\d+)", line)
            if m: rows.append([seed, *m.groups()])
    write(os.path.join(a.out, "seeds.csv"), ["seed", "strategy", "net", "gross", "fees", "fills"], rows)

    # research table, parsed from the saved report
    rr = []
    for line in open(os.path.join(ROOT, "docs", "research_report_synthetic.md")):
        if not (line.startswith("| ridge") or line.startswith("| sign(")): continue
        model, ic, null, p, ci, hit, r2 = [x.strip() for x in line.strip().strip("|").split("|")]
        nmin, nmax = re.findall(r"[+-][\d.]+", null); lo, hi = re.findall(r"[+-][\d.]+", ci)
        h, u = re.match(r"([\d.]+) \(([\d.]+)\)", hit).groups()
        rr.append([model, ic, nmin, nmax, p, lo, hi, h, u, r2])
    write(os.path.join(a.out, "research.csv"),
          ["model", "ic", "null_min", "null_max", "p_value", "ci_low", "ci_high", "hit_rate", "up_rate", "oos_r2"], rr)

    # BlazingMQ verification runs, as recorded in docs/blazingmq.md
    write(os.path.join(a.out, "transport.csv"), ["run", "published", "received", "retransmits", "book_matches_golden", "note"], [
        ["20k, queue limit raised", 20000, 20000, 0, "yes", ""],
        ["500k, queue limit raised", 500000, 500000, 0, "yes", ""],
        ["20k, stock 1,000-message limit", 20000, 17257, 2743, "yes",
         "Broker rejected posts; gaps filled by a simulated retransmit callback"]])
    print("wrote", len(os.listdir(a.out)), "files to", a.out)


if __name__ == "__main__":
    sys.exit(main())
