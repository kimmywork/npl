#!/usr/bin/env python3
"""Generate the M8 scaling probes: a syntax-rules ellipsis of N elements and
the same list without a macro. Usage: gen.py OUTDIR [N ...]"""
import pathlib
import sys

HEADER = "(import (scheme base) (scheme write))\n"


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    sizes = [int(n) for n in sys.argv[2:]] or [1000, 4000, 16000]
    for n in sizes:
        items = " ".join(str(i) for i in range(n))
        (out / f"macro-{n}.scm").write_text(
            HEADER
            + "(define-syntax my-list (syntax-rules () ((_ x ...) (list x ...))))\n"
            + f"(write (length (my-list {items})))\n")
        (out / f"plain-{n}.scm").write_text(
            HEADER + f"(write (length (list {items})))\n")


if __name__ == "__main__":
    main()
