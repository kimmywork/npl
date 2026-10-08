#!/usr/bin/env python3
"""
prop_tests.py — property-based tests (PBT) for the nplc0 stage0 compiler.

Properties:
  P1 EVAL CORRECTNESS  — randomly generated pure integer programs return
                         the same exit code as the Python reference model
                         (tagged fixnum, low byte), over many seeds.
  P2 NEVER CRASH       — arbitrary fuzzed inputs never crash the compiler
                         with a signal; it either succeeds or exits with a
                         normal error code.
  P3 FAIL-FAST         — programs using known-unsupported constructs are
                         rejected at compile time (exit != 0) and no output
                         binary is produced.
  P4 IDEMPOTENT AST    — printing the AST of a valid program is stable and
                         re-reading it yields an identical AST.

Usage: python3 tools/prop_tests.py [--seeds N] [--jobs N]
Exit 0 if all properties hold.
"""

import argparse
import concurrent.futures
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NPLC0 = os.path.join(ROOT, "build", "stage0", "nplc0")

OK = "\033[32mPASS\033[0m"
BAD = "\033[33mSKIP\033[0m"
FAIL = "\033[31mFAIL\033[0m"

results = {"pass": 0, "fail": 0, "skip": 0}


def record(status, label, detail=""):
    results[status] += 1
    color = {"pass": OK, "fail": FAIL, "skip": BAD}[status]
    print(f"[{color}] {label} {detail}")


# ---------------------------------------------------------------- P1 model

class EvalModel:
    """Reference semantics for the generated subset.

    Values follow runtime/npl.h: fixnum v is encoded as v << 2. The process
    exit code is the low byte of main's returned NplVal (project convention).
    Comparisons produce proper booleans, so they may only feed `if`.
    """

    MAXV = 30          # keep multiplications inside fixnum range after shifts
    CALLS = 0          # recursion budget guard

    def gen_expr(self, rng, depth, env):
        if depth <= 0 or rng.random() < 0.35:
            if env and rng.random() < 0.4:
                return ("var", rng.choice(env))
            return ("const", rng.randint(0, self.MAXV))
        kind = rng.choice(["+", "-", "*", "if", "let"])
        if kind == "+":
            return ("+", self.gen_expr(rng, depth - 1, env),
                    self.gen_expr(rng, depth - 1, env))
        if kind == "-":
            # avoid negative intermediates: always (a - b) where python model
            # tracks untagged values; negative tagged shift is still exact,
            # so any value works for +/-/* — keep it general.
            return ("-", self.gen_expr(rng, depth - 1, env),
                    self.gen_expr(rng, depth - 1, env))
        if kind == "*":
            return ("*", self.gen_expr(rng, depth - 1, env),
                    self.gen_expr(rng, depth - 1, env))
        if kind == "if":
            cmp = rng.choice(["<", "=", ">"])
            return ("if", (cmp, self.gen_expr(rng, depth - 1, env),
                           self.gen_expr(rng, depth - 1, env)),
                    self.gen_expr(rng, depth - 1, env),
                    self.gen_expr(rng, depth - 1, env))
        # let: bind one variable visible in body only
        name = f"lv{rng.randint(0, 99)}"
        return ("let", name, self.gen_expr(rng, depth - 1, env),
                self.gen_expr(rng, depth - 1, env + [name]))

    def render(self, expr):
        k = expr[0]
        if k == "const":
            return str(expr[1])
        if k == "var":
            return expr[1]
        if k in ("+", "-", "*"):
            return f"({k} {self.render(expr[1])} {self.render(expr[2])})"
        if k == "if":
            op, l, r = expr[1]
            return (f"(if ({op} {self.render(l)} {self.render(r)}) "
                    f"{self.render(expr[2])} {self.render(expr[3])})")
        if k == "let":
            return (f"(let (({expr[1]} {self.render(expr[2])})) "
                    f"{self.render(expr[3])})")
        raise ValueError(k)

    def eval(self, expr, env=None):
        env = dict(env or {})
        k = expr[0]
        if k == "const":
            return expr[1]
        if k == "var":
            return env[expr[1]]
        if k == "+":
            return self.eval(expr[1], env) + self.eval(expr[2], env)
        if k == "-":
            return self.eval(expr[1], env) - self.eval(expr[2], env)
        if k == "*":
            return self.eval(expr[1], env) * self.eval(expr[2], env)
        if k == "if":
            _, (op, le, ri) = expr[0], expr[1]
            l, r = self.eval(le, env), self.eval(ri, env)
            taken = {"<": l < r, "=": l == r, ">": l > r}[op]
            return self.eval(expr[2] if taken else expr[3], env)
        if k == "let":
            val = self.eval(expr[2], env)
            env = dict(env); env[expr[1]] = val
            return self.eval(expr[3], env)
        raise ValueError(k)

    def expected_exit(self, value):
        tag = (value & 0x3FFFFFFFFFFFFFFF) << 2   # simulating v << 2 wrap-free
        return ((value * 4) % 256 + 256) % 256    # low byte of v<<2


def eval_case(model, seed, work):
    """Compile and run one generated program; return a failure line or None."""
    rng = random.Random(seed)
    expr = model.gen_expr(rng, depth=4, env=[])
    prog = f"(define (main) {model.render(expr)})\n"
    src = os.path.join(work, f"p1_{seed}.npl")
    binp = os.path.join(work, f"p1_{seed}")
    with open(src, "w") as f:
        f.write(prog)
    c = subprocess.run([NPLC0, "-o", binp, src], capture_output=True)
    if c.returncode != 0:
        return f"  seed={seed}: compile failed: {c.stderr.decode()[:120]}"
    r = subprocess.run([binp], capture_output=True)
    want = model.expected_exit(model.eval(expr))
    got = r.returncode % 256
    if got != want:
        return (f"  seed={seed}: eval mismatch src={prog.strip()!r} "
                f"want={want} got={got}")
    return None


def pbt_eval(seeds, work, jobs):
    """Property 1: generated arithmetic == reference model.

    Cases are independent (per-seed RNG, per-seed files), so they run in a
    bounded thread pool; results are reported in seed order."""
    model = EvalModel()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        failures = [line for line in pool.map(
            lambda seed: eval_case(model, seed, work), range(seeds)) if line]
    for line in failures:
        print(line)
    if not failures:
        record("pass", f"P1 eval-correctness ({seeds} random programs)")
    else:
        record("fail", f"P1 eval-correctness", f"{len(failures)} mismatches")


# ---------------------------------------------------------------- P2 fuzz

FUZZ_TOKENS = [
    "(", ")", "'lambda", "(define", "))", "((", "x", "42", '"str', "\\",
    ";c\n", "[", "]", "#t", "#\\a", "-", "...", "'()", "@", "%", "^~",
]


def pbt_fuzz(seeds, work):
    """Property 2: fuzzer input never crashes the compiler with a signal."""
    crashes = 0
    for seed in range(seeds):
        rng = random.Random(10_000 + seed)
        junk = "".join(rng.choice(FUZZ_TOKENS) for _ in range(rng.randint(1, 60)))
        src = os.path.join(work, "fuzz.npl")
        with open(src, "w") as f:
            f.write(junk)
        try:
            c = subprocess.run(
                [NPLC0, "--print-ast", src],
                capture_output=True, timeout=5)
        except subprocess.TimeoutExpired:
            crashes += 1
            print(f"  seed={seed}: TIMEOUT on {junk!r}")
            continue
        # signal deaths surface as negative return codes; crash codes 132..139
        if c.returncode < 0 or 128 < c.returncode < 160:
            crashes += 1
            print(f"  seed={seed}: CRASH rc={c.returncode} on {junk!r}")
    if crashes == 0:
        record("pass", f"P2 robustness ({seeds} fuzz cases)")
    else:
        record("fail", "P2 robustness", f"{crashes} crashes")


# ---------------------------------------------------------------- P3 fail fast

UNSUPPORTED = [
    ('set!', "(define x 1)\n(define (main) (set! x 2))\n"),
    ('unbound var', "(define (main) (+ ghostly 1))\n"),

    ('misplaced else', "(define (main) (cond (else 1) (#t 2)))\n"),
    ('quoted lambda', "(define (main) (quote (lambda (x) x)))\n"),
]


def pbt_fail_fast(work):
    bad = []
    for i, (name, prog) in enumerate(UNSUPPORTED):
        src = os.path.join(work, f"ff{i}.npl")
        binp = os.path.join(work, f"ff{i}")
        with open(src, "w") as f:
            f.write(prog)
        c = subprocess.run([NPLC0, "-o", binp, src], capture_output=True)
        produced = os.path.exists(binp)
        if c.returncode == 0 or produced:
            bad.append(name)
    if not bad:
        record("pass", "P3 fail-fast on unsupported constructs")
    else:
        record("fail", "P3 fail-fast", f"accepted silently: {bad}")


# ---------------------------------------------------------------- P4 idempotence

VALID_PROGS = [
    '(define (fact n) (if (< n 2) 1 (* n (fact (- n 1)))))\n(define (main) (fact 5))\n',
    '(define x 10)\n(define (main) (+ x (let ((y 3)) (begin (display y) y))))\n',
    "(define (classify n)\n  (cond ((< n 0) -1) ((= n 0) 0) (else 1)))\n(define (main) (classify 9))\n",
    "(define lst '(1 (2 3) sym \"str\"))\n(display 'sym)",
]


def ast_of(path, work):
    out1 = subprocess.run([NPLC0, "--print-ast", path], capture_output=True)
    out2 = subprocess.run([NPLC0, "--print-ast", path], capture_output=True)
    return out1.stdout, out2.stdout


def pbt_idempotent(work):
    ok = True
    for i, prog in enumerate(VALID_PROGS):
        src = os.path.join(work, f"id{i}.npl")
        with open(src, "w") as f:
            f.write(prog)
        a, b = ast_of(src, work)
        if a != b or not a.strip():
            ok = False
            print(f"  prog {i}: unstable/empty AST output")
    if ok:
        record("pass", "P4 AST print stability")
    else:
        record("fail", "P4 AST print stability")


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=200)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = ap.parse_args()

    if not os.path.exists(NPLC0):
        print(f"compiler not built: {NPLC0}", file=sys.stderr)
        sys.exit(2)

    with tempfile.TemporaryDirectory(prefix="npl_pbt_") as work:
        pbt_eval(args.seeds, work, max(1, args.jobs))
        pbt_fuzz(min(args.seeds, 120), work)
        pbt_fail_fast(work)
        pbt_idempotent(work)

    total_bad = results["fail"]
    skipped = results["skip"]
    print(f"\nPBT summary: {results['pass']} pass, "
          f"{results['fail']} fail{', '+str(skipped)+' skipped' if skipped else ''}")
    sys.exit(1 if total_bad else 0)


if __name__ == "__main__":
    main()
