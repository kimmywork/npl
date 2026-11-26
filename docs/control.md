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

## Implementation boundary

The current CPS continuation is an opaque closure. Cutting or splicing it
requires the compiler-visible segment representation described in T4-A of
`.agents/design/threads.md`; that representation is not implemented yet.
Instead, a captured prefix is re-entered on a **helper native thread**:

- the runtime's cross-thread `call/cc` restores the captured dynamic
  environment (parameters, handlers, winders);
- the origin prompt's terminal is temporarily redirected to a result thunk;
- the invoker waits for the helper and receives the result, including multiple
  values;
- `dynamic-wind` before/after thunks run through the existing transition
  engine.

This preserves the observable prompt/abort/composition behavior exercised by
the current tests, but it is not the final architecture. In particular:

- composable invocation from a virtual thread blocks its carrier (use a native
  thread for now);
- invocations sharing one prompt are serialized, but a captured prefix must
  not recursively invoke the same origin (that would deadlock on the prompt
  mutex);
- winders outside the captured prompt are re-entered by the helper's
  cross-thread transfer; only winders between the prompt and the capture point
  are intended to run. This will be fixed by the T4-A segment representation;
- continuation barriers are not implemented;
- `call-in-continuation` / `call-in` / `return-to` are not implemented;
- there is no initial default prompt/continuation boundary, so omitted-tag
  captures require an explicit prompt; `call/cc` remains a full R7RS
  continuation rather than a default-prompt-delimited one;
- the continuation registry uses strong references, so repeatedly creating and
  discarding continuations retains their wrappers until a GC-aware weak
  registry is added;
- continuation marks, parameterization objects, fluids, promises, the
  exception-handler-stack condition, and the SRFI 226 thread/interrupt
  adapters remain future increments;
- helper-thread invocation costs a native thread creation per composition.

No changes were made to `bootstrap/current.ssa`; the prototype is a library over
the existing compiler and evaluator.

## Verification

- `src/compiler/tests/control.npl` — all current exports, prompt/abort,
  custom/default handlers, missing tags, `reset`/`shift`, multi-shot
  composition, 990/6930, predicates, multiple values, `dynamic-wind` and
  `unwind-protect`.
- `tests/fixtures/control` — deterministic nplc/npli surface through all
  component aliases.
- `runtime`/thread gates are unchanged and run in the normal full gate.
