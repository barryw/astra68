#!/usr/bin/env python3
"""Every documented header has a file comment and exactly one API page.

The Doxygen INPUT list is the set of documented headers. Each of them must
carry an @file block, and the API reference must render each with one
`doxygenfile` directive -- a header missing from the pages is undocumented
in the manual however complete its comments are, and a group directive would
render its members a second time.
"""

from pathlib import Path
import re


def doxygen_inputs(ndk: Path) -> list[Path]:
    text = (ndk / "docs/Doxyfile").read_text(encoding="utf-8")
    match = re.search(r"^INPUT\s*=\s*((?:[^\n]*\\\n)*[^\n]*)", text,
                      re.MULTILINE)
    if match is None:
        raise SystemExit("docs/Doxyfile: no INPUT")
    headers = []
    for entry in match.group(1).replace("\\\n", " ").split():
        path = ndk / entry
        headers += sorted(path.glob("*.h")) if path.is_dir() else [path]
    return headers


def main() -> None:
    ndk = Path(__file__).resolve().parents[1]
    repo = ndk.parent
    headers = doxygen_inputs(ndk)
    problems = [f"{path.relative_to(repo)}: no @file documentation"
                for path in headers
                if "@file" not in path.read_text(encoding="utf-8")]

    pages = sorted((ndk / "docs/source/api").glob("*.md"))
    rendered: dict[str, list[str]] = {}
    for page in pages:
        text = page.read_text(encoding="utf-8")
        if "{doxygengroup}" in text:
            problems.append(f"{page.relative_to(repo)}: group directive; "
                            "render headers with doxygenfile")
        for name in re.findall(r"^```\{doxygenfile\}\s+(\S+)", text,
                               re.MULTILINE):
            rendered.setdefault(name, []).append(page.name)

    names = {path.name for path in headers}
    for name in sorted(names):
        where = rendered.get(name, [])
        if len(where) != 1:
            problems.append(f"{name}: rendered by {len(where)} API pages "
                            f"{where}, expected 1")
    for name in sorted(set(rendered) - names):
        problems.append(f"{name}: rendered but not in the Doxygen INPUT")

    if problems:
        raise SystemExit("NDK documentation coverage:\n  " +
                         "\n  ".join(problems))
    print(f"NDK documentation coverage: PASS ({len(names)} headers)")


if __name__ == "__main__":
    main()
