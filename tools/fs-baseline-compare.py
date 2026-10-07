#!/usr/bin/env python3
"""Compare two fs-baseline records against the PRD's release budget.

usage: fs-baseline-compare.py BASE.json NEW.json [--budget 0.10]

A workload passes when its median and p95 are at most (1 + budget) times
the baseline's, and its throughput, when it has one, is at least
(1 - budget) times the baseline's. Exit 0 when every workload present in
both passes, 1 otherwise. docs/FILESYSTEM_PRODUCTION_PRD.md, "Acceptance
and performance gates".
"""

import argparse
import json
import sys


def workloads(path):
    with open(path) as source:
        record = json.load(source)
    return record.get("guest", record)["workloads"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base")
    parser.add_argument("new")
    parser.add_argument("--budget", type=float, default=0.10)
    arguments = parser.parse_args()
    base = workloads(arguments.base)
    new = workloads(arguments.new)
    failed = False
    for name in sorted(set(base) & set(new)):
        old, now = base[name], new[name]
        verdicts = []
        for key in ("median_ns", "p95_ns"):
            ratio = now[key] / old[key] if old[key] else 1.0
            ok = ratio <= 1.0 + arguments.budget
            verdicts.append("%s %.2fx%s" % (key, ratio, "" if ok else "!"))
            failed |= not ok
        if old.get("throughput_bps") and now.get("throughput_bps"):
            ratio = now["throughput_bps"] / old["throughput_bps"]
            ok = ratio >= 1.0 - arguments.budget
            verdicts.append("throughput %.2fx%s" % (ratio, "" if ok else "!"))
            failed |= not ok
        if now.get("failed"):
            verdicts.append("FAILED-RUN")
            failed = True
        print("%-16s %s" % (name, "  ".join(verdicts)))
    print("BUDGET %s" % ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
