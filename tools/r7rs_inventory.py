#!/usr/bin/env python3
"""Compare R7RS-small Appendix A exports with NPL library exports.

Usage: python3 tools/r7rs_inventory.py [--missing] [--library NAME]

Reads docs/reference/r7rs.html (Appendix A) and lib/scheme/*.sld. A name
counts as present when the library exports it or, for syntax handled by the
compiler core, when it is listed in CORE_SYNTAX. Presence is an export check,
not a conformance verdict: behavior is established by tests/r7rs fixtures.
"""

import argparse
import glob
import html
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Syntax the compiler frontend implements directly for every program; these
# are not library exports in NPL (see src/compiler/frontend/expand.npl).
CORE_SYNTAX = {
    "base": {"...", "=>", "_", "else", "unquote", "unquote-splicing",
             "begin", "cond-expand", "define", "define-syntax",
             "define-values", "if", "include", "include-ci", "lambda",
             "let", "let*", "let-syntax", "let-values", "letrec", "letrec*",
             "letrec-syntax", "quasiquote", "quote", "set!", "syntax-error",
             "syntax-rules"},
}


def standard_exports():
    text = open(os.path.join(ROOT, "docs/reference/r7rs.html")).read()
    appendix = text[text.index('<h2 id="a">'):]
    appendix = appendix[:appendix.find("<h2", 10)]
    libraries = {}
    for match in re.finditer(
            r'<h3 id="scheme-([a-z0-9-]+)">.*?</h3>(.*?)(?=<h3 id=|\Z)',
            appendix, re.S):
        names = []
        for block in re.findall(r"<pre>(.*?)</pre>", match.group(2), re.S):
            names += html.unescape(re.sub(r"<[^>]+>", "", block)).split()
        # Appendix sections also contain example code; keep identifiers only.
        libraries[match.group(1)] = {n for n in names
                                     if "(" not in n and ")" not in n}
    return libraries


def library_exports(path):
    text = re.sub(r";[^\n]*", "", open(path).read())
    start = text.index("(export")
    depth, index = 0, start
    while True:
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                break
        index += 1
    body = text[start + len("(export"):index]
    names = {m.group(1) for m in
             re.finditer(r"\(rename\s+\S+\s+([^\s()]+)\)", body)}
    body = re.sub(r"\(rename\s+\S+\s+[^\s()]+\)", " ", body)
    return names | set(body.split())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--missing", action="store_true",
                        help="list missing names")
    parser.add_argument("--library", help="restrict to one library")
    args = parser.parse_args()

    standard = standard_exports()
    npl = {os.path.basename(p)[:-4]: library_exports(p)
           for p in glob.glob(os.path.join(ROOT, "lib/scheme/*.sld"))}
    total_standard = total_present = 0
    for library in sorted(standard):
        if args.library and library != args.library:
            continue
        expected = standard[library]
        present = expected & (npl.get(library, set())
                              | CORE_SYNTAX.get(library, set()))
        missing = sorted(expected - present)
        total_standard += len(expected)
        total_present += len(present)
        state = "absent" if library not in npl else ""
        print(f"{library:16} {len(present):3}/{len(expected):3} {state}")
        if args.missing and missing:
            print("  missing: " + " ".join(missing))
    print(f"{'total':16} {total_present:3}/{total_standard:3}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
