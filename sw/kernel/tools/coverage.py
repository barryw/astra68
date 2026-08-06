#!/usr/bin/env python3
"""Union line coverage over the kernel's host suites, per source file.

Run through `make coverage`, which builds the suites instrumented and cleans
up afterwards. A line counts as covered if any suite executed it, which is the
question worth asking of a kernel: the modules share sources, and a line
exercised only by test_dispatch is still a line under test.

What the number is not: a measure of whether the behaviour is checked. It says
a line ran, not that anything asserted what it did -- and the m68k assembly,
the PMMU walk and the platform MMIO cannot run on the host at all, so they are
outside it entirely. Read it for the direction of travel and for finding
modules nobody has exercised, not as a grade.
"""

import glob
import gzip
import json
import os
import subprocess
import sys

BUILD = "build"


def main():
    for stale in glob.glob("*.gcov.json.gz"):
        os.unlink(stale)

    gcda = sorted(glob.glob(os.path.join(BUILD, "*.gcda")))
    for path in gcda:
        subprocess.run(["gcov", "-i", "-o", BUILD, path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=False)

    covered = {}
    total = {}
    for report in glob.glob("*.gcov.json.gz"):
        with gzip.open(report, "rt") as handle:
            data = json.load(handle)
        for entry in data.get("files", []):
            name = os.path.basename(entry["file"])
            if not name.endswith(".c"):
                continue
            for line in entry.get("lines", []):
                key = line["line_number"]
                total.setdefault(name, set()).add(key)
                if line.get("count", 0) > 0:
                    covered.setdefault(name, set()).add(key)

    rows = []
    for name in sorted(total):
        hit = len(covered.get(name, set()))
        all_lines = len(total[name])
        rows.append((name, hit, all_lines, 100.0 * hit / all_lines))

    kernel = [row for row in rows if not row[0].startswith("test_")]
    kernel.sort(key=lambda row: row[3])
    print("%-22s %8s %8s %8s" % ("source", "covered", "lines", "percent"))
    for name, hit, all_lines, percent in kernel:
        print("%-22s %8d %8d %7.1f%%" % (name, hit, all_lines, percent))
    hit = sum(row[1] for row in kernel)
    lines = sum(row[2] for row in kernel)
    print("%-22s %8d %8d %7.1f%%" % ("TOTAL (kernel sources)", hit, lines,
                                     100.0 * hit / lines if lines else 0.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
