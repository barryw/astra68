#!/usr/bin/env python3
"""Require every public NDK header to participate in the strict Doxygen gate."""

from pathlib import Path


def main() -> None:
    ndk = Path(__file__).resolve().parents[1]
    repo = ndk.parent
    headers = sorted((ndk / "include/astra").glob("*.h"))
    headers += [
        repo / "sw/include/astra/gui.h",
        repo / "sw/include/astra/message_abi.h",
    ]

    missing = [str(path.relative_to(repo)) for path in headers
               if "@file" not in path.read_text(encoding="utf-8")]
    if missing:
        raise SystemExit("public headers missing @file documentation:\n  " +
                         "\n  ".join(missing))


if __name__ == "__main__":
    main()
