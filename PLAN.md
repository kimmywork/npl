# NPL Plan

## 1. Stage0 Frontend

Status: complete.

Build an untyped C99 reader with top-level `include` expansion.

Acceptance: `./tools/check_stage0.sh` verifies merged AST order, relative includes, malformed input, missing files, invalid include paths, and cycles.

Evidence: `stage0 frontend: PASS`.

## 2. Stage0 Compiler

Emit tagged QBE for functions, recursion, `cond`, closures, tail calls, and included files.

Acceptance: stage0 compiles and runs the bootstrap fixture suite.

## 3. First NPL Compiler

Implement the compiler as modules under `src/compiler/` using the stage0 language.

Acceptance:

```text
nplc0(src/compiler/main.npl) == bootstrap/stage1.ssa
qbe(stage1.ssa) -> nplc1
nplc1 compiles and runs the bootstrap fixtures
```

## 4. Historical Relay

Evolve the compiler one generation at a time. Check in each canonical SSA; keep each generation's source in Git history.

Acceptance:

```text
nplcN(next sources) == bootstrap/stageN+1.ssa
qbe(stageN+1.ssa) -> nplcN+1
```

When a generation compiles the same sources as its successor, require byte-identical SSA.

## 5. Language

Add to the NPL compiler in this order:

1. R7RS dynamic core and libraries.
2. Gradual typing and unboxed lowering.
3. Numeric tower and NPL base libraries.
4. Tensor values, autograd, and NN modules.

Each increment must reproduce its canonical SSA and pass its focused language tests.
