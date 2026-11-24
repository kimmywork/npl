# Review probes

Small programs that reproduce the findings in the 2026-11-14 reviews. They are
evidence, not gate tests: when a finding is fixed, turn its probe into a
`src/compiler/tests/` suite case or a `tests/r7rs/fixtures/` differential case.

```sh
.agents/reviews/probes/run.sh .agents/reviews/probes/2026-11-14-macro-system/cases/*.scm
.agents/reviews/probes/run.sh --scale 1000 4000 16000      # M8 timing
```

`run.sh` needs `build/` (qbe, runtime, `npli`); it builds nplc from
`bootstrap/current.ssa` into `$PROBE_WORK` (default
`${TMPDIR:-/tmp}/npl-review-probes`). Guile and Racket columns appear when the
tools are installed. Each directory keeps its observed output in
`observed.txt`; the first line records the commit and tools used.

## 2026-11-14-syntax-rules-ellipsis

Review: [syntax-rules ellipsis and lambda shorthand](../2026-11-14-syntax-rules-ellipsis-and-lambda-shorthand.md).

| Probe | Shows |
|---|---|
| `01-original-macro.scm` | The user's `\` macro: rule 2 matches `(x . rest)` first (npli); Guile rejects the pattern. |
| `02-original-two-arrows.scm` | N1: two `=>` make `=>` a formal; npli prints 3. |
| `03-greedy-last-arrow.scm` | N1: the last `=>` wins. |
| `04-reordered.scm` | Reordered rules, still using N1 leniency. |
| `05-strict-r7rs.scm` | Portable R7RS version (`fn`): npli, nplc, Guile agree. |
| `06-curry.scm` | Curried version; relies on N1, so Guile rejects it. |
| `07-reject-multiple-arrows.scm` | `syntax-error` on a second `=>`. |
| `08-backslash-identifier.scm` | N2: npli and Guile read `\`, nplc does not. |
| `09-import-free-assv.scm` | N3: `assv` unbound without imports (npli and nplc). |
| `racket-*.rkt` | Racket: original pattern rejected, strict version, `syntax-parse` version and its error. |
| `scan_ellipsis.py` | Heuristic scan for same-level multiple ellipses: `python3 scan_ellipsis.py FILE...`. |

## 2026-11-14-macro-system

Review: [macro system](../2026-11-14-macro-system-review.md).

| Probe | Finding |
|---|---|
| `cases/04`, `cases/26` | M2: depth-0 variable in an ellipsis template |
| `cases/10` | M4: top-level macro-introduced definitions |
| `cases/11` | M3: alias-layer any-mark capture |
| `cases/13` | M5: body macro defined after the lambda that uses it |
| `cases/20`, `cases/21` | M9: diagnostics without location |
| `cases/22` | M7: template depth checked only at use |
| `cases/25`, `cases/27` | M10: SRFI 149 template extensions |
| `cases/28`, `cases/31`-`33` | M6: core syntax and import sets |
| `cases/29`, `body-define/*` | M1: macro-produced single `define` in a body (`shadow-proc` is the nplc miscompile) |
| `scale/gen.py` | M8: quadratic transcription, via `run.sh --scale` |
| other `cases/*` | Hygiene and pattern cases where npli, nplc, and Guile agree |

`cases/24` uses `define-record-type`, which Guile needs SRFI 9 for, so its
Guile column is not a comparison. `body-define/leak.scm` checks that npli does
not leak the body definition to the top level; its second form is expected
to fail.

## 2026-11-23-ffi-posix-time

Review: [0.1.3 FFI, POSIX, and time](../2026-11-23-0.1.3-ffi-posix-time-review.md).
Uses `build/bin/npli` and `build/bin/nplc` directly (the driver links the
core native packages); `observed.txt` records the output at `32cfa51`.

```sh
.agents/reviews/probes/2026-11-23-ffi-posix-time/run.sh            # 01-05, 09-11
.agents/reviews/probes/2026-11-23-ffi-posix-time/run.sh --leak     # R5
.agents/reviews/probes/2026-11-23-ffi-posix-time/run.sh --cc       # R6
.agents/reviews/probes/2026-11-23-ffi-posix-time/run.sh --stubs    # R7
```

| Probe | Finding |
|---|---|
| `01`, `02` | R1: directory handle use after close |
| `03` | R2: zone offset sign (Guile column) |
| `04` | R3: unchecked integer arguments in `(npl unix)` |
| `05` | R4: view read after `foreign-free` |
| `06` | R5: `make-c-struct` leak (`--leak`) |
| `07-import-*` | R6: C compiler at import (`--cc`) |
| `09` | R8: `npl_time_now_ts` declared but not defined |
| `10` | R9: mismatched brackets (Guile column) |
| `11` | R10: stub headers conflict with a libc prototype |

## 2026-11-24-ffi-posix-time-round2

Review: [0.1.3 FFI, POSIX, and time, round 2](../2026-11-24-0.1.3-ffi-posix-time-review-round2.md).
Uses `build/bin/npli` and `build/bin/nplc` directly; `observed.txt` records
the output at `1b1f931`, including a rerun of the round-1 probes.

```sh
.agents/reviews/probes/2026-11-24-ffi-posix-time-round2/run.sh           # 13-15, 17-18, 20-22
.agents/reviews/probes/2026-11-24-ffi-posix-time-round2/run.sh --gc      # R13
.agents/reviews/probes/2026-11-24-ffi-posix-time-round2/run.sh --link    # R16
.agents/reviews/probes/2026-11-24-ffi-posix-time-round2/run.sh --cache   # R19
```

| Probe | Finding |
|---|---|
| `13` | R13: `foreign-alloc` block store without a write barrier (`--gc` for small nurseries and `NPL_GC_VERIFY`) |
| `14` | R14: freed c-struct passed to a `define-c-function` (SIGSEGV) |
| `15` | R15: stale directory handle revived by address reuse |
| `16` | R16: `NPL_NATIVE_LINK` ignored (`--link`) |
| `17` | R17: `(npl process)` and `(npl unix)` both export `delete-directory` |
| `18` | R18: typedef-named struct argument in a generated stub |
| `19-*` | R19: `NPL_C_STRUCT_LAYOUTS` hides the core layout cache (`--cache`) |
| `20` | R20: in-process layout cache keyed only by C type |
| `21` | R21: bignum seconds and flags in `(npl unix)` |
| `22` | R22: shebang accepted by npli, rejected by nplc |
