# NPL Plan

## 1. Stage0 Executable Compiler

Status: complete.

- Keep stage0 untyped.
- Support `cond` and relative top-level `include`.
- Emit QBE SSA with `--emit-qbe`.
- Build executables with `-o` through QBE and the runtime.

Acceptance: reader, include, arithmetic, recursion, closure, tail-call, and executable tests pass.

Evidence: `tools/check_stage0.sh` and `tools/check_stage0_qbe.sh` pass.

## 2. Permanent Bootstrap Compiler

Status: complete.

- Keep sources under `bootstrap/compiler/`.
- Support the fixed bootstrap language and source inclusion.
- Generate `bootstrap/bootstrap.ssa`.
- Rebuild from that SSA and reproduce it byte for byte.

Acceptance: `tools/check_bootstrap.sh` verifies the seed build, canonical SSA, fixed point, and bootstrap fixture programs.

Evidence: the checked-in 6,251-line `bootstrap/bootstrap.ssa` is reproduced byte for byte.

## 3. Current Full Compiler Baseline

Status: complete.

- Keep evolving sources under `src/compiler/`.
- Generate `bootstrap/current.ssa` with the permanent bootstrap compiler.
- Rebuild the full compiler from canonical SSA.

Acceptance: the restored full compiler emits and runs the bootstrap fixture programs.

Evidence: the checked-in 6,251-line `bootstrap/current.ssa` is reproducible and restores the current compiler.

## 4. R7RS Core

Implement lexical forms, proper tail calls, hygienic `syntax-rules`, multiple values, full continuations, exceptions, parameters, promises, records, bytevectors, ports, and the R7RS library system.

Acceptance: focused conformance tests for each feature and library pass through the full compiler.

## 5. Gradual Types

Implement annotations, local HM inference, Dynamic boundaries, unboxed scalar lowering, function and collection types, records with row polymorphism, and restricted refinements.

Acceptance: inference, rejection, guard, and QBE lowering tests pass through the full compiler.

## 6. Libraries and Numeric Tower

Implement NPL base libraries, vectors and strings, exact bignums, rationals, complex numbers, and floating-point operations.

Acceptance: R7RS library tests pass and exact `100!` produces the expected integer.

## 7. Tensors and Neural Networks

Implement typed tensors, broadcasting, matrix multiplication, reductions, reverse-mode autograd, and static `nn-module` expansion on the CPU backend.

Acceptance: tensor shape tests, gradient checks, dense-layer forward evaluation, and XOR training pass.

## 8. Release Gate

- Reproduce `bootstrap/bootstrap.ssa` and `bootstrap/current.ssa`.
- Restore both NPL compilers using only QBE SSA and runtime sources.
- Run all language and runtime suites from a fresh build directory.
