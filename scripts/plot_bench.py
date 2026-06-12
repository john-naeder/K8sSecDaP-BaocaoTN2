#!/usr/bin/env python3
"""
Run libdsa frequency benchmark and emit CSV + pgfplots data files for the report.

Usage:
    python3 scripts/plot_bench.py [--bench BINARY] [--out OUTDIR]

Defaults:
    BINARY = libdsa/build/benchmarks/bench_frequency
    OUTDIR = output/bench

Emits (under OUTDIR):
    throughput.csv       record/estimate throughput by algorithm & width
    memory.csv           memory footprint by algorithm & width
    error_rate.csv       CMS overcount vs width
    *.dat                pgfplots-compatible data (space-separated, #-header)
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BIN = REPO_ROOT / "libdsa" / "build" / "benchmarks" / "bench_frequency"
DEFAULT_OUT = REPO_ROOT / "output" / "bench"


def run_bench(binary: Path) -> dict:
    if not binary.exists():
        sys.exit(f"error: benchmark binary not found at {binary}\n"
                 f"build first: cmake --build libdsa/build --target bench_frequency")
    result = subprocess.run(
        [str(binary), "--benchmark_format=json", "--benchmark_min_time=0.3s"],
        check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def write_throughput(benchmarks: list, out: Path) -> None:
    rows = []
    for b in benchmarks:
        name = b["name"]
        items_per_sec = b.get("items_per_second", 0.0)
        mem_kb = b.get("memory_kb", "")
        if name.startswith("BM_CMS_Record/"):
            w = name.split("/")[1]
            rows.append(("CMS_Record", w, items_per_sec, mem_kb))
        elif name == "BM_CMS_Estimate":
            rows.append(("CMS_Estimate", "2048", items_per_sec, ""))
        elif name == "BM_HashMap_Record":
            rows.append(("HashMap_Record", "-", items_per_sec, mem_kb))
        elif name == "BM_HashMap_Estimate":
            rows.append(("HashMap_Estimate", "-", items_per_sec, ""))

    csv_path = out / "throughput.csv"
    with csv_path.open("w") as f:
        f.write("algorithm,width,ops_per_sec,memory_kb\n")
        for r in rows:
            f.write(f"{r[0]},{r[1]},{r[2]:.0f},{r[3]}\n")

    dat_path = out / "throughput.dat"
    with dat_path.open("w") as f:
        f.write("# width ops_per_sec (CMS_Record)\n")
        for r in rows:
            if r[0] == "CMS_Record":
                f.write(f"{r[1]} {r[2]:.0f}\n")


def write_error_rate(benchmarks: list, out: Path) -> None:
    rows = []
    for b in benchmarks:
        if b["name"].startswith("BM_CMS_ErrorRate/"):
            w = int(b["name"].split("/")[1])
            overcount = b.get("avg_overcount", 0.0)
            rows.append((w, overcount))
    rows.sort()

    csv_path = out / "error_rate.csv"
    with csv_path.open("w") as f:
        f.write("width,eps_theoretical,avg_overcount\n")
        for w, oc in rows:
            eps = 2.71828 / w
            f.write(f"{w},{eps:.5f},{oc:.3f}\n")

    dat_path = out / "error_rate.dat"
    with dat_path.open("w") as f:
        f.write("# width avg_overcount eps_bound (m=100000)\n")
        for w, oc in rows:
            eps = 2.71828 / w
            bound = eps * 100000
            f.write(f"{w} {oc:.3f} {bound:.0f}\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", type=Path, default=DEFAULT_BIN)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    data = run_bench(args.bench)
    benchmarks = data.get("benchmarks", [])

    write_throughput(benchmarks, args.out)
    write_error_rate(benchmarks, args.out)

    (args.out / "bench_raw.json").write_text(json.dumps(data, indent=2))
    print(f"wrote {args.out / 'throughput.csv'}")
    print(f"wrote {args.out / 'error_rate.csv'}")
    print(f"wrote {args.out / 'throughput.dat'}")
    print(f"wrote {args.out / 'error_rate.dat'}")
    print(f"wrote {args.out / 'bench_raw.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
