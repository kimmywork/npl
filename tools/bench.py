#!/usr/bin/env python3
"""P1 performance baseline: NPL native, npli, Guile, and Racket.

Usage: python3 tools/bench.py [--runs N] [--timeout SECONDS] [workload ...]

Each bench/workloads/<name>.scm defines (bench-main) using R7RS-small only.
The runner wraps it per implementation, compiles where the implementation
compiles (nplc; Guile's cache by a warm-up run; raco make), checks that every
implementation prints the same output, then times N runs. Compile time is
reported separately from run time. Times are the median and the min-max
range of wall-clock and child CPU (user+sys) seconds; CPU time is less
sensitive to host load. A run that exceeds the timeout is reported as such.
"""

import argparse
import os
import resource
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKLOADS = os.path.join(ROOT, "bench", "workloads")
NPLC = os.path.join(ROOT, "build", "bin", "nplc")
NPLI = os.path.join(ROOT, "build", "bin", "npli")
RACKET_BIN = os.environ.get(
    "RACKET_BIN", "")
IMPORTS = "(import (scheme base) (scheme write) (scheme char))\n"
# Racket needs no imports (racket/base covers R7RS-small base), but it does not
# accept a two-branch `if` and has no `string-upcase`. This header supplies
# both so the workload source is byte-identical to the other implementations.
# The two-branch form falls through to an unspecified value, as R7RS allows.
RACKET_HEADER = (
    "#lang racket\n"
    "(define-syntax (if stx)\n"
    "  (syntax-case stx ()\n"
    "    ((_ p c) #'(cond (p c)))\n"
    "    ((_ p c a) #'(cond (p c) (else a)))))\n"
    "(define (string-upcase s)\n"
    "  (list->string (map char-upcase (string->list s))))\n"
)


def timed(command, timeout, cwd):
    """Run once; return (wall, cpu, stdout) or None on timeout/failure."""
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter()
    try:
        result = subprocess.run(command, cwd=cwd, capture_output=True,
                                text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    wall = time.perf_counter() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    if result.returncode != 0:
        sys.stderr.write("%s failed (%d): %s\n" % (
            command[0], result.returncode, result.stderr.strip()[-300:]))
        return None
    return wall, cpu, result.stdout


def prepare(name, body, work):
    """Write per-implementation sources; return {impl: (compile, run)}."""
    def write(file, text):
        path = os.path.join(work, file)
        with open(path, "w") as out:
            out.write(text)
        return path
    native = write(name + ".npl", IMPORTS + body + "\n(define (main) (bench-main) 0)\n")
    script = write(name + ".scm", IMPORTS + body + "\n(bench-main)\n")
    racket = write(name + ".rkt", RACKET_HEADER + body + "\n(bench-main)\n")
    exe = os.path.join(work, name)
    return {
        "native": ([NPLC, native, "-o", exe], [exe]),
        "npli": (None, [NPLI, script]),
        "guile": (["guile", "--r7rs", "-q", "-s", script],
                  ["guile", "--r7rs", "-q", "-s", script]),
        "racket": ([os.path.join(RACKET_BIN, "raco"), "make", racket],
                   [os.path.join(RACKET_BIN, "racket"), racket]),
    }


def summary(samples):
    if not samples:
        return "timeout"
    walls = [s[0] for s in samples]
    cpus = [s[1] for s in samples]
    return "%8.3f [%.3f-%.3f]  cpu %8.3f" % (
        statistics.median(walls), min(walls), max(walls),
        statistics.median(cpus))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("workloads", nargs="*")
    args = parser.parse_args()
    names = args.workloads or sorted(
        f[:-4] for f in os.listdir(WORKLOADS) if f.endswith(".scm"))
    print("host load: %s" % os.getloadavg()[0])
    print("%-9s %-7s %-10s %s" % ("workload", "impl", "compile", "run wall median [min-max]  cpu median"))
    with tempfile.TemporaryDirectory() as work:
        for name in names:
            with open(os.path.join(WORKLOADS, name + ".scm")) as source:
                body = source.read()
            outputs = {}
            for impl, (compile_cmd, run_cmd) in prepare(name, body, work).items():
                compile_text = "-"
                if compile_cmd:
                    compiled = timed(compile_cmd, args.timeout * 5, work)
                    if compiled is None:
                        print("%-9s %-7s compile failed" % (name, impl))
                        continue
                    compile_text = "%.2fs" % compiled[0]
                warm = timed(run_cmd, args.timeout, work)
                if warm is None:
                    print("%-9s %-7s %-10s timeout (> %.0fs)" % (
                        name, impl, compile_text, args.timeout))
                    continue
                outputs[impl] = warm[2]
                samples = []
                for _ in range(args.runs):
                    sample = timed(run_cmd, args.timeout, work)
                    if sample is None:
                        break
                    samples.append(sample)
                print("%-9s %-7s %-10s %s" % (name, impl, compile_text, summary(samples)))
            if len(set(outputs.values())) > 1:
                print("%-9s OUTPUT MISMATCH: %s" % (
                    name, {k: v.strip()[:40] for k, v in outputs.items()}))


if __name__ == "__main__":
    main()
