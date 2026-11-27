# Control features (SRFI 226 prototype)

`(npl control)` implements the first SRFI 226 control layer: continuation
prompts, `abort-current-continuation`, non-composable and composable
continuations, `call-in-continuation`/`call-in`/`return-to`, continuation
barriers, continuation predicates, `unwind-protect`, and derived
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
chain, and installing the same parameterization again preserves in-place
updates to cells already in that chain. A `parameter-set!` for a parameter
with no binding in the snapshot extends the snapshot's own chain, so the next
installation sees it. New cells are isolated from the surrounding
parameterization. A cell already shared with an enclosing parameterization is
still mutated in place and is visible outside the snapshot; that shared-cell
behavior and the copy-on-write race are the D0 replacement work, not a claim
of full SRFI 226 parameter semantics. Cross-thread installation follows NPL's
existing thread-owned copy-on-write rules, so ordinary shared-cell behavior
across threads and the separate inheritable thread-local kind remain T4-D
work. `temporarily` accepts any parameter-like
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

The compiler now emits an explicit **continuation segment** at every
non-direct user procedure call boundary while a prompt is active. A segment
wraps the local CPS continuation and records its semantic parent, a stable
frame id, and the nearest enclosing prompt. The chain is the shared
compiler/evaluator representation used by prompts and barriers:

- `NplDynamicContext` captures the current segment and the prompt chain;
  the existing wind/context transfer reinstates both on the invoking thread;
- `NplThreadContext` roots the current segment, and virtual-thread
  save/restore carries it with the other dynamic state;
- a prompt frame records its origin segment and a barrier flag;
- `call/cc` capture and invocation go through the segment wrapper, so a
  captured prefix retains non-tail frame boundaries rather than an opaque
  one-piece closure;
- the evaluator's native control operators use the same captured-continuation
  and wind-transfer path.

Prompts and composable invocation still use **runtime prompt frames** plus a
per-thread one-shot meta-continuation override:

- a prompt frame holds its tag, handler, escape/terminal continuation, outer
  return continuation, origin segment, and barrier flag; the frame chain lives
  in `NplThreadContext`, is captured by `NplDynamicContext`, and is reinstated
  by the existing wind/context transfer;
- invoking a composable continuation installs the invoker's continuation as a
  one-shot override in the invoking thread's runtime context, then applies the
  captured prefix on the **same logical thread**;
- the delimiter's outer prompt link is spliced to the invoking prompt chain
  for the duration of the activation. The captured prefix keeps its own winds,
  handlers, parameters and inner prompts, while the invoking continuation's
  prompts remain visible: `continuation-prompt-available?` sees them, aborts
  to their tags reach their handlers, and the delimiter remains the nearest
  same-tag prompt;
- when the prefix reaches the prompt terminal, it consumes the override,
  removes the splice and returns its values to the invoker; the invoker's
  later return through the same prompt uses the ordinary terminal;
- non-composable invocation installs the nearest cut prompt's escape as the
  override, so the prefix is reinstated at that cut point;
- virtual-thread save/restore carries the prompt chain and overrides with its
  other dynamic state.

This removes the earlier helper-native-thread re-entry and the user-visible
`thread-specific` override. Logical thread identity is preserved, virtual
workers are not blocked by a carrier join, outer winders are not re-entered by
a cross-thread transfer, and multiple values/multi-shot composition use the
same path. Prompt frames survive a captured `call/cc`, and
`tests/unit/control-virtual.npl` exercises 990/6930, `reset`/`shift`,
barriers and call-in inside virtual threads on M=1 and M=4 carriers.

Continuation barriers are prompt frames with an inaccessible tag and a barrier
mark. Capture records the prompt prefix; a non-composable invocation rejects a
jump that would reinstate a barrier frame absent from the invoker's current
chain, while outward escapes remain allowed. A composable capture whose prefix
contains a barrier raises a continuation violation. Barrier identity is frame
identity, not a depth counter.

`call-in-continuation` uses a control packet. The captured continuation first
performs its normal dynamic-context transfer; the generated continuation then
installs this activation's prompt override in the *target* environment and
either delivers the packet's values to the saved successor or calls the
supplied procedure with that successor. `call-in` and `return-to` are the
non-composable conveniences. Composable call-in uses the invocation's
`return-k`, so the captured prefix's terminal returns to the caller while the
procedure itself runs at the capture point. Every invocation has an independent
return relation; captured descriptions are immutable.

`prompt_overrides` is part of `NplDynamicContext`. A non-local exit transfers to
its target context and drops overrides that belonged to the abandoned
activation; an aborted composition therefore leaves no pending override for a
later invocation. Installing the override after the transfer (rather than
before) is what makes that lifecycle correct.

User-visible continuations are runtime `BOX_CONTINUATION` objects carrying
their kind, origin prompt, tag, prompt prefix and graft entry. Compiled code and
evaluated library instances therefore share continuation identity and
predicates; there is no per-library strong registry retaining discarded
wrappers.

Each program, native thread and virtual thread installs a runtime default
prompt at its initial continuation. The root prompt stores the dynamic
environment outside itself. An abort to the default root performs the normal
wind/context transfer first, so intervening `dynamic-wind` after thunks run
and the default handler sees the root parameters, handlers and prompt chain;
the handler's result then completes the program or thread. Generated main runs
under `npl_program_run`, whose C landing pad also unwinds a root abort raised
inside a nested synchronous callback (for example a hash/equivalence
procedure). Native threads complete through their termination jump and virtual
threads through the carrier's mount landing pad; a single value is the join
result and zero/multiple values arrive as the runtime values object. Full R7RS
`call/cc` remains undelimited: installing the default prompt does not silently
change existing `call/cc` behavior.

Remaining T4-C+ limits:

- evaluated user closures are direct-style native procedures, not
  compiler-emitted CPS segments. The shared runtime continuation objects,
  prompt frames and native control operators cover capture/invocation across
  the native/evaluated boundary, but continuation marks over arbitrary
  evaluated helper frames still need the T4-C evaluator frame adapter;
- runtime-created transition continuations (wind before/after thunks,
  parameter converters and promise work) do not yet install a compiler
  segment, so capturing inside those thunks has incomplete frame metadata;
- the interactive REPL uses the process root prompt; a per-evaluation REPL
  default boundary belongs with the later initial-continuation work;
- continuation marks, thread parameters, fluids, promises, the
  exception-handler-stack condition, and the SRFI 226 thread/interrupt
  adapters remain future increments (ordinary parameterization snapshots and
  `temporarily` are implemented).

The frontend/backend segment emission changes the compiler's own emitted SSA,
so `bootstrap/current.ssa` was relayed with the codegen-transient mode of
`tools/update_current_ssa.sh`; the installed seed is gen2 with
`gen2 == gen3` (441,853 lines, SHA-256 `32ffe87c…`). The permanent bootstrap compiler, stage0 and
`bootstrap/runtime/` are untouched.

## Verification

- `src/compiler/tests/control.npl` — all current exports, prompt/abort,
  custom/default handlers, missing tags, prompt frames captured by `call/cc`,
  `reset`/`shift`, multi-shot composition, 990/6930, predicates, multiple
  values, `dynamic-wind`, `unwind-protect`, barrier escape/rejection,
  `call-in-continuation`/`call-in`/`return-to`, composable call-in, default
  prompt captures and aborted-activation cleanup.
- `src/compiler/tests/control-parameters.npl` — parameter snapshots,
  `call-with-parameterization`, `temporarily`, restoration on raise and
  continuation exit.
- `tests/unit/control-virtual.npl` — composition, `reset`/`shift`, barriers
  and call-in inside virtual threads on M=1 and M=4 carriers.
- `tests/unit/control-default-prompt.npl` — program, native-thread and
  virtual-thread default prompt roots, omitted-tag captures and default
  handler aborts.
- `tests/unit/control-b0.npl` — single-value root return, root abort unwind and
  handler result for native/virtual threads, root abort across a synchronous
  hash callback, parameterization cell persistence, and the program-level root
  landing pad.
- `tests/fixtures/control` and `tests/fixtures/control-parameters` —
  deterministic nplc/npli surface through all component aliases, including
  barrier and call-in cases.
- `runtime`/thread gates are unchanged and run in the normal full gate.
