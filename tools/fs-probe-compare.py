#!/usr/bin/env python3
"""Compare fs-probe records: the lane against the direct baseline.

usage: fs-probe-compare.py BASE[,BASE...] NEW[,NEW...]
                           [--budget 0.10] [--cross-stat 0.05]
                           [--cross-read 0.5] [--hostfs-ns 500]
                           [--syscalls 0.1]

A side's value per phase is the median over its runs. NEW passes when its
stat and read medians and p95s are within the budget of BASE, and when three
structural guards hold for both phases -- the lane runs no server on the
data path:
- cross-address-space switches per operation exceed BASE's by at most
  --cross-stat / --cross-read. A relay adds 2-4 per op; background
  preemption hits both paths alike, and in the read window it alone moves
  the count by up to about 1.5 between runs, so read's margin is wider;
- HostFS CPU per operation is at or under --hostfs-ns;
- client syscalls per operation exceed BASE's by at most --syscalls.
"""

import argparse
import json
import statistics
import sys

FIELDS = ("median_ns", "p95_ns", "cross_per_op", "hostfs_ns_per_op",
          "client_syscalls_per_op")


def side(spec):
    runs = []
    for path in spec.split(","):
        with open(path) as source:
            runs.append(json.load(source)["phases"])
    return {phase: {field: statistics.median(run[phase][field]
                                             for run in runs)
                    for field in FIELDS}
            for phase in ("stat", "read")}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base")
    parser.add_argument("new")
    parser.add_argument("--budget", type=float, default=0.10)
    parser.add_argument("--cross-stat", type=float, default=0.05)
    parser.add_argument("--cross-read", type=float, default=0.5)
    parser.add_argument("--hostfs-ns", type=float, default=500.0)
    parser.add_argument("--syscalls", type=float, default=0.1)
    arguments = parser.parse_args()
    base = side(arguments.base)
    new = side(arguments.new)
    failed = []
    for phase in ("stat", "read"):
        for field in ("median_ns", "p95_ns"):
            ratio = new[phase][field] / base[phase][field]
            print("%-5s %-9s base %10.0f new %10.0f ratio %.3f" %
                  (phase, field, base[phase][field], new[phase][field],
                   ratio))
            if ratio > 1.0 + arguments.budget:
                failed.append("%s %s %.3f" % (phase, field, ratio))
        cross = new[phase]["cross_per_op"] - base[phase]["cross_per_op"]
        calls = (new[phase]["client_syscalls_per_op"] -
                 base[phase]["client_syscalls_per_op"])
        print("%-5s cross/op base %.3f new %.3f delta %+.3f  "
              "hostfs ns/op %.0f  syscalls/op base %.2f new %.2f "
              "delta %+.2f" %
              (phase, base[phase]["cross_per_op"],
               new[phase]["cross_per_op"], cross,
               new[phase]["hostfs_ns_per_op"],
               base[phase]["client_syscalls_per_op"],
               new[phase]["client_syscalls_per_op"], calls))
        if cross > (arguments.cross_stat if phase == "stat" else
                    arguments.cross_read):
            failed.append("%s cross %+.3f" % (phase, cross))
        if calls > arguments.syscalls:
            failed.append("%s syscalls %+.2f" % (phase, calls))
        if new[phase]["hostfs_ns_per_op"] > arguments.hostfs_ns:
            failed.append("%s hostfs %.0f" % (
                phase, new[phase]["hostfs_ns_per_op"]))
    print("PROBE FAIL %s" % "; ".join(failed) if failed else "PROBE PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
