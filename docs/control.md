# Control features (SRFI 226 prototype)

`(npl control)` implements the first SRFI 226 control layer: continuation
prompts, `abort-current-continuation`, non-composable and composable
continuations, continuation predicates, `unwind-protect`, and derived
`reset`/`shift`. Component libraries and singular SRFI aliases are available:

```scheme
(import (except (scheme base) call/cc call-with-current-continuation)
        (npl control))

(import (srfi 226 prompt))        ; prompts and violations
(import (srfi 226 continuation))  ; continuation kinds
(import (srfi 226 shift-reset))   ; reset / shift
(import (srfi 226 inspection))    ; continuation predicates
(import (srfi 226 parameter))     ; parameterizations and temporarily
```

The component export is a **T4-A/T4-B prototype**, not a complete SRFI 226
release. The composite `(srfi 226)` and the `srfi-226` feature are deliberately
not advertised yet.

## Import conflict

SRFI 226 redefines `call/cc` and `call-with-current-continuation` as ordinary
procedures so they can be wrapped and inspected. `(scheme base)` exports the
primitive control operators under the same names, so an importing program must
exclude one set:

```scheme
(import (except (scheme base) call/cc call-with-current-continuation)
        (npl control))
```

## Prompts and aborts

```scheme
(define tag (make-continuation-prompt-tag 'work))

(call-with-continuation-prompt
  (lambda ()
    (+ 1 (abort-current-continuation tag 'cancel)))
  tag
  list)                                  ; => (cancel)
```

The default abort handler reinstalls the prompt and calls the thunk passed to
`abort-current-continuation`. A custom handler receives the abort objects. A
missing tag raises a `continuation-violation?` condition whose prompt tag is
available through `continuation-violation-prompt-tag`.

`(default-continuation-prompt-tag)` is a zero-argument procedure returning the
singleton default tag; `continuation-prompt-tag?` recognizes both explicit and
default tags.

## Non-composable and composable continuations

```scheme
(+ 1 (reset (* 2 (shift k 4))))          ; => 5
(+ 1 (reset (* 2 (shift k (k 4)))))      ; => 9
(+ 1 (reset (* 2 (shift k (k (k 4))))))  ; => 17
```

`call-with-non-composable-continuation` cuts the current continuation back to
the nearest matching prompt and reinstates the captured prefix there.
`call-with-composable-continuation` appends the prefix to the current
continuation and returns its values to the caller. The reference examples that
distinguish the two kinds produce `990` and `6930` respectively and are part of
the acceptance suite.

`call/cc` and `call-with-current-continuation` from `(npl control)` return
inspected, non-composable continuations. `continuation?` and
`non-composable-continuation?` are backed by an explicit registry, so ordinary
procedures do not satisfy them.

## Parameterizations and `temporarily`

`(npl control parameters)` / `(srfi 226 parameter)` adds first-class ordinary
parameterization snapshots:

```scheme
(define p (make-parameter 10 (lambda (x) (* x x))))
(p 12)                                    ; p is now 144

(define saved
  (parameterize ((p (p)))
    (current-parameterization)))

(call-with-parameterization
  saved
  (lambda ()
    (let ((before (p)))
      (p 0)
      (list before (p)))))                ; => (20736 0)
```

`parameter?` recognizes existing parameter objects. A parameterization maps
parameter objects to mutable cells: capturing shares the current binding
chain, and installing the same parameterization again preserves those cells.
A `parameter-set!` made through a snapshot persists in that parameterization,
while the surrounding parameterization keeps its own cells. Cross-thread
installation still follows NPL's existing thread-owned copy-on-write rules,
so ordinary shared-cell behavior across threads and the separate inheritable
thread-local kind remain T4-D work. `temporarily` accepts any parameter-like
procedure and swaps its saved values
with the current values on every entry and exit through `dynamic-wind`. Because
the values are installed through the parameter's setter, a non-idempotent
converter can transform the restored value (the SRFI 226 example with a `+1`
converter ends at `3`, not the initial `2`).

NPL's existing parameters already use thread-owned copy-on-write bindings, so
this prototype does not yet expose SRFI 226's separate ordinary shared-cell
and inheritable-thread-local parameter kinds, `make-thread-parameter`, or the
fluid syntax. Those remain T4-D work.

## Implementation boundary

The current CPS continuation is still an opaque closure. Prompts and
composable invocation are implemented with a **prompt segment record plus a
per-thread one-shot meta-continuation override**:

- a prompt record holds its tag, handler, escape/terminal continuation and
  outer return continuation;
- invoking a composable continuation installs the invoker's continuation as a
  one-shot override on the origin prompt, then applies the captured prefix on
  the **same logical thread**;
- when the prefix reaches the prompt terminal, it consumes the override and
  returns its values to the invoker; the invoker's later return through the
  same prompt uses the ordinary terminal;
- non-composable invocation installs the nearest cut prompt's escape as the
  override, so the prefix is reinstated at that cut point.

This removes the earlier helper-native-thread re-entry. Logical thread identity
is preserved, virtual workers are not blocked by a carrier join, outer winders
are not re-entered by a cross-thread transfer, and multiple values/multi-shot
composition use the same path. `tests/unit/control-virtual.npl` exercises
990/6930 and `reset`/`shift` inside virtual threads on M=1 and M=4 carriers.

This is still not the compiler-visible segmented-continuation representation
required by T4-A. In particular:

- the captured prefix remains a full CPS closure; there is no compiler/evaluator
  segment or semantic-frame metadata for marks, shared-prefix identity or
  barriers;
- the override lives in `thread-specific`, so the implementation temporarily
  occupies that user-visible slot during composition;
- continuation barriers are not implemented;
- `call-in-continuation` / `call-in` / `return-to` are not implemented;
- there is no initial default prompt/continuation boundary, so omitted-tag
  captures require an explicit prompt; `call/cc` remains a full R7RS
  continuation rather than a default-prompt-delimited one;
- the continuation registry uses strong references, so repeatedly creating and
  discarding continuations retains their wrappers until a GC-aware weak
  registry is added;
- thread parameters, fluids, continuation marks, promises, the
  exception-handler-stack condition, and the SRFI 226 thread/interrupt
  adapters remain future increments (ordinary parameterization snapshots and
  `temporarily` are implemented).

No changes were made to `bootstrap/current.ssa`; the prototype is a library over
the existing compiler and evaluator. The next accepted stage is the
compiler/evaluator shared semantic-frame and segmented-continuation
representation; only then should the same-thread override be replaced, followed
by barriers/`call-in`, shared-vs-thread-local parameter cells, marks, fluids,
promises and interrupts. The composite `(srfi 226)` feature is not published
until that inventory is complete.

## Verification

- `src/compiler/tests/control.npl` — all current exports, prompt/abort,
  custom/default handlers, missing tags, `reset`/`shift`, multi-shot
  composition, 990/6930, predicates, multiple values, `dynamic-wind` and
  `unwind-protect`.
- `src/compiler/tests/control-parameters.npl` — parameter snapshots,
  `call-with-parameterization`, `temporarily`, restoration on raise and
  continuation exit.
- `tests/unit/control-virtual.npl` — same-thread composition inside virtual
  threads on M=1 and M=4 carriers.
- `tests/fixtures/control` and `tests/fixtures/control-parameters` —
  deterministic nplc/npli surface through all component aliases.
- `runtime`/thread gates are unchanged and run in the normal full gate.
