#!/usr/bin/env python3
"""Check that each exported name of the new data libraries appears in a test.

This is a coverage floor, not a semantic proof: a name counts as covered when
the corresponding compiler test mentions it. The semantic suites assert
behavior; this check catches an export that no test even references.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LIBRARIES = {
    "lib/npl/comparator.sld": ["src/compiler/tests/srfi-128.npl"],
    "lib/npl/data/set.sld": ["src/compiler/tests/srfi-113.npl"],
    "lib/npl/data/bag.sld": ["src/compiler/tests/srfi-113.npl"],
    "lib/npl/sort.sld": ["src/compiler/tests/srfi-132.npl"],
    "lib/npl/data/collection.sld": ["src/compiler/tests/srfi-44.npl",
                                     "src/compiler/tests/srfi-44-surface.npl"],
    "lib/npl/data/map.sld": ["src/compiler/tests/srfi-146.npl",
                             "src/compiler/tests/srfi-146-surface.npl"],
    "lib/npl/data/bitvector.sld": ["src/compiler/tests/srfi-178.npl",
                                    "src/compiler/tests/srfi-178-surface.npl"],
}

TOKEN = re.compile(r"[A-Za-z0-9!?*/<>=+._-]+")


def exports(path):
    text = (ROOT / path).read_text()
    match = re.search(r"\(export\s+(.*?)\)\s*\(import", text, re.S)
    if not match:
        raise SystemExit("%s: no export declaration" % path)
    return set(match.group(1).split())


def main():
    failed = False
    for library, tests in LIBRARIES.items():
        tokens = set()
        for test in tests:
            tokens.update(TOKEN.findall((ROOT / test).read_text()))
        missing = sorted(exports(library) - tokens)
        if missing:
            failed = True
            print("%s: %d exported names not mentioned in tests" %
                  (library, len(missing)))
            print("  " + " ".join(missing))
    if failed:
        return 1
    print("library test coverage: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
