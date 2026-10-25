#!/usr/bin/env python3
"""Compare fs-baseline records against the PRD's release budget.

usage: fs-baseline-compare.py BASE[,BASE...] NEW[,NEW...] [--budget 0.10]

Each side uses the per-workload median over its runs. Any failed run or
workload absent from one run fails the comparison.

A workload passes when its median and p95 are at most (1 + budget) times
the baseline's, and its throughput, when it has one, is at least
(1 - budget) times the baseline's. A workload present in only one of the
two records is a FAIL naming it: a partial run must not read as a clean
comparison. Exit 0 when every workload is present in both and passes,
1 otherwise. docs/FILESYSTEM_PRODUCTION_PRD.md, "Acceptance and
performance gates".
"""

import argparse
import json
import statistics
import sys


def workloads(spec):
    runs = []
    for path in spec.split(","):
        with open(path) as source:
            record = json.load(source)
        runs.append(record.get("guest", record)["workloads"])
    merged = {}
    for name in set.union(*(set(run) for run in runs)):
        entries = [run[name] for run in runs if name in run]
        merged[name] = {
            "median_ns": statistics.median(e["median_ns"] for e in entries),
            "p95_ns": statistics.median(e["p95_ns"] for e in entries),
            "failed": len(entries) != len(runs) or
                      any(e.get("failed") for e in entries),
        }
        rates = [e.get("throughput_bps") for e in entries]
        if all(rate is not None for rate in rates):
            merged[name]["throughput_bps"] = statistics.median(rates)
        elif any(rate is not None for rate in rates):
            merged[name]["failed"] = True
    return merged


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base")
    parser.add_argument("new")
    parser.add_argument("--budget", type=float, default=0.10)
    arguments = parser.parse_args()
    base = workloads(arguments.base)
    new = workloads(arguments.new)
    failed = not base or not new
    for name in sorted(set(base) - set(new)):
        print("%-16s MISSING (only in base)" % name)
        failed = True
    for name in sorted(set(new) - set(base)):
        print("%-16s MISSING (only in new)" % name)
        failed = True
    for name in sorted(set(base) & set(new)):
        old, now = base[name], new[name]
        verdicts = []
        for key in ("median_ns", "p95_ns"):
            ratio = now[key] / old[key] if old[key] else 1.0
            ok = ratio <= 1.0 + arguments.budget
            verdicts.append("%s %.2fx%s" % (key, ratio, "" if ok else "!"))
            failed |= not ok
        if old.get("throughput_bps") and "throughput_bps" in now:
            ratio = now["throughput_bps"] / old["throughput_bps"]
            ok = ratio >= 1.0 - arguments.budget
            verdicts.append("throughput %.2fx%s" % (ratio, "" if ok else "!"))
            failed |= not ok
        elif old.get("throughput_bps"):
            verdicts.append("MISSING-THROUGHPUT")
            failed = True
        if old.get("failed") or now.get("failed"):
            verdicts.append("FAILED-RUN")
            failed = True
        print("%-16s %s" % (name, "  ".join(verdicts)))
    print("BUDGET %s" % ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
