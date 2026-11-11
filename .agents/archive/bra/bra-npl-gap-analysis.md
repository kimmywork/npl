# Bra→NPL Gap Analysis

## Executive Summary

**Date:** 2026-11-06  
**Analyzed:** Bra CAS repository  
**Target:** Rewrite Bra in NPL

### Verdict on Uncommitted Changes

**Status:** All uncommitted changes (32 files, +2693/-1230) should be **COMMITTED**.

**Rationale:**
- All tests pass (2550/2550)
- Substantive improvements: integration chain-rule engine, matrix decompositions (Cholesky/SVD/least-squares), solve, improved limit/series
- Cross-validated against SymPy/NumPy
- Lint warnings are minor (trailing whitespace + 1 pre-existing file-length)
- workstate.md documents the changes and their verification

**Recommendation:** Commit this work before starting the NPL port. These are real capabilities, not WIP.

---

## Current Bra Architecture

### Code Volume
- **Total:** ~12,581 lines of Racket (49 files)
- **Core modules:** ~6,149 lines (algebra + calculus)
- **GUI:** ~1,619 lines (notebook interface)
- **Tests:** ~4,000+ lines

### Module Structure

```
bra/
├── core/
│   ├── expr.rkt          (370 lines) — math-symbol, bra-expr, normalized arithmetic
│   └── result.rkt         — ComputeResult protocol
├── algebra/
│   ├── polynomial.rkt    (799 lines) — Karatsuba, heuristic GCD
│   ├── matrix.rkt        (738 lines) — LU, QR, eigenvalues
│   ├── decomposition.rkt (359 lines) — Cholesky, SVD, least-squares
│   ├── rational.rkt      (531 lines) — partial fractions, Hermite
│   ├── multivariate.rkt  (1009 lines) — Gröbner bases
│   ├── simplify.rkt      (225 lines) — expand through poly domain
│   ├── solve.rkt         (207 lines) — real roots
│   ├── factor.rkt        (279 lines) — with certificates
│   ├── gcd.rkt           (38 lines) — dispatch
│   └── modular.rkt       (247 lines)
├── calculus/
│   ├── integrate.rkt     (576 lines) — chain-rule engine
│   ├── series.rkt        (430 lines) — truncated power-series ring
│   ├── limit.rkt         (321 lines) — structural growth analysis
│   └── derivative.rkt    (160 lines)
├── numeric/
│   └── evaluate.rkt       — machine-double evaluation
├── rewrite/
│   └── {pattern,rule,engine}.rkt — pattern matching
├── gui/
│   ├── notebook.rkt      (999 lines) — Racket GUI desktop app
│   ├── notebook-controller.rkt (70 lines)
│   └── value-presenter.rkt (129 lines)
├── notebook.rkt          (421 lines) — document/cell model
├── runtime.rkt            — trusted REPL namespace
├── render/
│   └── {latex,bitmap,svg,terminal}.rkt — multi-backend rendering
└── main.rkt               — public API (110 exports)
```

### Key Dependencies
- **Racket base:** pattern matching, lists, strings, ports, classes
- **Racket GUI (`gui-lib`):** `racket/gui/base`, `framework`
- **No external C libraries:** all algorithms in pure Racket

---

## NPL Capability Gaps

### 1. **Language Features** ⚠️ Major Gap

| Feature | Bra (Racket) | NPL Status | Gap Size |
|---------|-------------|-----------|----------|
| Structs with mutability | `struct #:transparent #:mutable` | No structs | **LARGE** |
| Pattern matching | `match`, `match-define` | None | **LARGE** |
| Classes/objects | `racket/class` | None | **LARGE** |
| Keyword arguments | `(foo #:key val)` | None | Medium |
| Optional/rest args | `(lambda (a [b 0] . rest) ...)` | Only rest | Small |
| Multiple values | `(values a b)` / `(define-values)` | Has `values` | ✓ Complete |
| Modules | `(provide ...)` / `(require ...)` | R7RS libraries | ✓ Equivalent |
| Macros | `define-syntax-rule`, `syntax-case` | R7RS `syntax-rules` | Partial |

**Impact:** The lack of structs and pattern matching is the **single biggest blocker**. Bra's core data model (`math-symbol`, `bra-expr`, `notebook-cell`, `notebook-document`) relies heavily on transparent structs with field accessors. NPL would need:
- Either: add structs (large language change)
- Or: encode as tagged lists/vectors + manual accessors (verbose, error-prone)

### 2. **Standard Library** ⚠️ Major Gap

| Capability | Racket | NPL | Gap |
|-----------|--------|-----|-----|
| List operations | `foldl`, `foldr`, `filter`, `partition`, `append*` | Basic | Medium |
| String operations | `string-split`, `string-join`, `string-trim` | Basic | Medium |
| Hash tables | `hash`, `hash-ref`, `hash-set` | None | **LARGE** |
| Sets | `set`, `set-add`, `set-member?` | None | Medium |
| File I/O | `file->string`, `string->file`, paths | Ports only | Medium |
| Pretty-printing | `pretty-print`, `pretty-format` | `write` only | Small |

**Impact:** Hash tables are used throughout Bra for memoization and caching. NPL would need either:
- Add hash-table primitives (moderate runtime work)
- Use association lists (O(n) lookups → performance loss)

### 3. **GUI / Desktop App** ❌ **CRITICAL BLOCKER**

Bra's GUI depends on:
- `racket/gui/base` — native desktop widgets (Cocoa/GTK/Windows)
- `framework` — DrRacket's editor infrastructure
- `editor%`, `canvas%`, `text%` — rich text editing
- Editor snips — embedded editors within editors

**NPL has:** None of this.

**Options:**
1. **FFI to a C GUI library** (GTK, Qt, SDL) — **months of work**
2. **Web-based notebook** (HTML5 + WebAssembly) — different architecture
3. **Terminal UI** (like `rlwrap`) — loses visual rendering
4. **Defer GUI entirely** — focus on CAS core first

**Recommendation:** Start with option 4. The CAS core (algebra/calculus) is independent of GUI. A notebook format can serialize/deserialize without rendering.

### 4. **Rendering Pipeline** ⚠️ Moderate Gap

Bra renders formulas to:
- **Bitmap** (canvas drawing) — needs pixel buffer + drawing primitives
- **SVG** (XML generation) — needs string builder (doable in NPL)
- **LaTeX** (string generation) — fully portable to NPL ✓
- **Terminal** (box-drawing Unicode) — fully portable ✓

**NPL has:**
- String ports ✓
- Bytevector I/O ✓
- No native graphics API

**Gap:** Bitmap rendering requires either FFI to Cairo/Skia or rasterization in NPL. SVG/LaTeX/terminal are **fully portable**.

### 5. **Numeric Performance** ⚠️ Moderate Gap

Bra uses:
- Exact rationals — NPL has this ✓
- Arbitrary-precision integers (GMP via Racket) — NPL has this (runtime/bignum.c) ✓
- Inexact flonums — NPL has this ✓
- Complex numbers — NPL has this ✓

**But:** Bra's polynomial and matrix code uses:
- Racket vectors with mutation — NPL has vectors ✓
- Unsafe operations (`unsafe-vector-ref`) — NPL has none
- Karatsuba in Racket — portable to NPL, but will be slower

**Gap:** Performance-critical inner loops (matrix multiply, polynomial multiply) will be slower in NPL without unsafe ops or C primitives.

### 6. **Cross-Validation Infrastructure** ⚠️ External Dependency

Bra's correctness depends on:
- **SymPy** (Python) for algebra/calculus — invoked via subprocess
- **NumPy/SciPy** (Python + BLAS/LAPACK) for numerical — subprocess
- **Hypothesis** (Python PBT) — property-based testing

**NPL has:** Process spawning (`run-process`) ✓

**Gap:** The cross-validation harness can be ported, but depends on having Python + SymPy + NumPy installed. The PBT suite is Python-only; NPL would need its own test generator or keep the Python harness.

---

## Reusability Analysis

### ✅ **Directly Portable** (~40% of code)

These modules use only R7RS + portable patterns:

1. **Core expression model** (`bra/core/expr.rkt`): `math-symbol`, `bra-expr`, arithmetic normalization
   - **Blocker:** Needs structs. Workaround: tagged vectors + manual accessors.
   - **Effort:** 2-3 days to rewrite.

2. **Polynomial arithmetic** (`bra/algebra/polynomial.rkt`): coefficient lists, Karatsuba
   - **Portable:** 90%. Uses vectors + fold + recursion.
   - **Effort:** 1 week (rewrite, test, cross-validate).

3. **Rational functions** (`bra/algebra/rational.rkt`): partial fractions, Hermite reduction
   - **Portable:** 85%. Depends on polynomial + GCD.
   - **Effort:** 1 week.

4. **Calculus** (`bra/calculus/{derivative,integrate,limit,series}.rkt`):
   - **Portable:** 80%. Heavy use of pattern matching → needs manual `cond`/`case` rewrite.
   - **Effort:** 2-3 weeks for all four modules.

5. **LaTeX/terminal rendering** (`bra/render/{latex,terminal}.rkt`):
   - **Portable:** 95%. String generation only.
   - **Effort:** 3-5 days.

6. **Notebook document model** (`bra/notebook.rkt`): cells, serialization
   - **Portable:** 70%. Struct-heavy, but serialization is string-based.
   - **Effort:** 1 week (rewrite structs as records/vectors).

### ⚠️ **Portable with Effort** (~30% of code)

7. **Matrix algebra** (`bra/algebra/matrix.rkt`, `decomposition.rkt`):
   - **Blocker:** Performance. Naive loops will be 10-100x slower than LAPACK.
   - **Options:**
     - Pure NPL (slow but correct)
     - FFI to BLAS/LAPACK (C bindings)
   - **Effort:** 2 weeks (pure NPL), 4-6 weeks (with FFI).

8. **GCD/factorization** (`bra/algebra/{gcd,factor}.rkt`):
   - **Portable:** 75%. Subresultant PRS is pure algorithm.
   - **Effort:** 1-2 weeks.

9. **Multivariate** (`bra/algebra/multivariate.rkt`): Gröbner bases
   - **Portable:** 60%. Heavy pattern matching + monomial orders.
   - **Effort:** 2-3 weeks (complex algorithm).

10. **Rewrite engine** (`bra/rewrite/`):
    - **Blocker:** Pattern matching is core to the design.
    - **Options:**
      - Port pattern compiler to NPL macros (hard)
      - Manual pattern dispatch (verbose)
    - **Effort:** 2-4 weeks.

### ❌ **Not Portable** (~30% of code)

11. **GUI** (`bra/gui/`): Racket `framework`, `editor%`, `canvas%`
    - **Status:** **Cannot port.** Needs entire GUI stack.
    - **Alternative:** Web-based notebook (different architecture).

12. **Bitmap rendering** (`bra/graphics/bitmap.rkt`):
    - **Needs:** Pixel buffer + drawing API.
    - **Alternative:** FFI to Cairo/Skia, or skip bitmap rendering.

---

## FFI Feasibility

### What Can Be Delegated to C?

| Component | C Library | Effort | Priority |
|-----------|-----------|--------|----------|
| Linear algebra | BLAS/LAPACK | High (4-6 weeks) | **HIGH** — performance-critical |
| Hash tables | `khash.h` or custom | Medium (2 weeks) | **HIGH** — used everywhere |
| Pattern matching | Custom | High (6-8 weeks) | Medium — manual dispatch works |
| GUI | GTK/Qt/ImGui | Very High (3-6 months) | **LOW** — web UI is better |
| Bitmap rendering | Cairo/Skia | High (4-6 weeks) | Low — SVG/LaTeX suffice |

**Recommended FFI priorities:**
1. **Hash tables** — small, high-value, unblocks memoization.
2. **BLAS/LAPACK** — only if numerical performance matters.
3. Skip GUI/bitmap — do web-based notebook instead.

---

## NPL Core Enhancements Needed

To make Bra→NPL feasible, NPL needs:

### **P0 (Required)**

1. **Structs or records** with named fields and pattern matching.
   - **Alternative:** Use vectors + convention, but error-prone.
   - **Effort:** 4-8 weeks to add structs to NPL.

2. **Hash tables** (either FFI or runtime primitive).
   - **Effort:** 2 weeks (FFI), 3-4 weeks (native).

### **P1 (Highly Desirable)**

3. **Pattern matching** (`match` or `syntax-case`).
   - **Alternative:** Manual `cond` rewrite (verbose).
   - **Effort:** 6-10 weeks for full `match` macro.

4. **Richer standard library:**
   - `foldl`, `foldr`, `partition`, `filter` (2-3 days)
   - `string-split`, `string-join`, `string-trim` (1 week)
   - `hash`, `hash-ref`, `hash-set` (covered above)

### **P2 (Nice to Have)**

5. **Keyword arguments** — convenience, not essential.
6. **Classes** — can encode with closures.
7. **Unsafe vector ops** — performance, not correctness.

---

## Recommended Path Forward

### **Phase 1: Core CAS (No GUI)** — 3-4 months

**Goal:** Port algebra/calculus core to NPL, verify against SymPy.

**Scope:**
- Expression model (structs as vectors)
- Polynomial, rational, GCD, factorization
- Derivative, integrate, limit, series
- LaTeX/terminal rendering
- Cross-validation harness (Python subprocess)

**Effort:**
- 8-12 weeks of focused work
- ~6,000 lines of NPL (down from 12,000 Racket due to less abstraction)

**Deliverables:**
- `(npl cas)` library
- SymPy cross-validation suite
- Benchmark report vs SymPy

**Blockers to resolve first:**
- Add hash tables to NPL (2 weeks)
- Decide struct encoding (vectors vs add structs to NPL)

### **Phase 2: Matrix Algebra** — 1-2 months

**Goal:** Port matrix operations, decide on LAPACK FFI.

**Scope:**
- Matrix, LU, QR, eigenvalues
- Cholesky, SVD, least-squares
- NumPy cross-validation

**Effort:**
- 4-8 weeks (pure NPL: 4 weeks, with LAPACK FFI: +4 weeks)

**Decision point:** Is 10-100x slower than NumPy acceptable? If no → FFI.

### **Phase 3: Notebook (Web UI)** — 2-3 months

**Goal:** Web-based notebook (not desktop GUI).

**Architecture:**
- Backend: NPL HTTP server (or Racket bridge)
- Frontend: HTML5 + MathJax/KaTeX for rendering
- Protocol: JSON-RPC or WebSocket

**Effort:**
- 8-12 weeks (full-stack)

**Alternative:** Keep Racket GUI, add NPL as compute backend via IPC.

---

## Bottom Line

### Can Bra be rewritten in NPL?

**Yes, but with significant effort and architectural changes.**

### What's feasible today?

**The CAS core** (algebra/calculus) can be ported in 3-4 months if:
1. NPL gains hash tables (2 weeks of NPL work)
2. Structs are encoded as vectors (verbose but works)
3. Pattern matching is manually expanded (tedious but finite)

**The GUI** cannot be ported without months of FFI work or a pivot to web.

### What NPL code is reusable?

**~40%** is directly portable (expression model, polynomials, calculus).  
**~30%** is portable with effort (matrix, multivariate, rewrite).  
**~30%** is not portable (GUI, bitmap rendering).

### Biggest blockers?

1. **No structs** — pervasive, affects every module.
2. **No hash tables** — blocks memoization (performance loss).
3. **No GUI stack** — blocks desktop notebook.

### Should we proceed?

**Recommendation:** 
1. **Commit the uncommitted Bra changes** (they're good).
2. **Prototype Phase 1** (core CAS) as an NPL library over 2-4 weeks:
   - Port `expr.rkt` (vectors as structs)
   - Port one calculus module (e.g., `derivative.rkt`)
   - Add hash-table primitive to NPL runtime
   - Measure effort/pain points
3. **Decide after prototype:** Full port vs keep Bra in Racket vs hybrid.

---

## Appendix: File-by-File Portability

| File | Lines | Portable? | Effort | Blockers |
|------|-------|-----------|--------|----------|
| `bra/core/expr.rkt` | 370 | Yes | 3 days | Structs → vectors |
| `bra/core/result.rkt` | 150 | Yes | 2 days | Structs |
| `bra/algebra/polynomial.rkt` | 799 | Yes | 1 week | None (pure algorithm) |
| `bra/algebra/matrix.rkt` | 738 | Partial | 2 weeks | Performance (no LAPACK) |
| `bra/algebra/rational.rkt` | 531 | Yes | 1 week | Depends on polynomial |
| `bra/algebra/simplify.rkt` | 225 | Yes | 3 days | Pattern matching → cond |
| `bra/algebra/gcd.rkt` | 38 | Yes | 1 day | Dispatch only |
| `bra/algebra/factor.rkt` | 279 | Yes | 1 week | Pattern matching |
| `bra/algebra/solve.rkt` | 207 | Yes | 5 days | Bisection + exact roots |
| `bra/algebra/decomposition.rkt` | 359 | Partial | 2 weeks | Performance |
| `bra/algebra/multivariate.rkt` | 1009 | Partial | 3 weeks | Complex, pattern-heavy |
| `bra/algebra/modular.rkt` | 247 | Yes | 1 week | Pure algorithm |
| `bra/calculus/derivative.rkt` | 160 | Yes | 2 days | Pattern matching → cond |
| `bra/calculus/integrate.rkt` | 576 | Yes | 2 weeks | Chain-rule engine |
| `bra/calculus/limit.rkt` | 321 | Yes | 1 week | Structural analysis |
| `bra/calculus/series.rkt` | 430 | Yes | 1 week | Power-series ring |
| `bra/numeric/evaluate.rkt` | 150 | Yes | 2 days | Eval + substitute |
| `bra/rewrite/*.rkt` | 400 | Partial | 3 weeks | Pattern compiler |
| `bra/notebook.rkt` | 421 | Yes | 1 week | Serialization only |
| `bra/render/latex.rkt` | 300 | Yes | 3 days | String generation |
| `bra/render/terminal.rkt` | 200 | Yes | 2 days | Box-drawing |
| `bra/render/svg.rkt` | 250 | Yes | 3 days | XML generation |
| `bra/render/bitmap.rkt` | 300 | No | N/A | Needs pixel API |
| `bra/gui/*.rkt` | 1619 | No | N/A | Needs GUI framework |
| **Total (portable)** | **~6500** | — | **12-16 weeks** | — |
