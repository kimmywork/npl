# QBE backend capabilities

Status: source audit and isolated probes, 2026-11-24. No backend or runtime
behavior changed. Scope: vendored QBE 1.3 and the current NPL backend;
opportunities below are recommendations, not scheduled implementation.

## Findings

QBE aggregate types describe memory layout for C ABI calls. They are not
first-class structured SSA values and do not allocate storage. `data` emits
static storage; `alloc4/8/16` allocates function-lifetime stack storage; NPL's
runtime allocates managed heap objects. These mechanisms are complementary.

Primary specification: [QBE IL](../../external/qbe-1.3/doc/il.txt), sections
Types, Constants and Vals, Linkage, Definitions, Memory, Call, and Phi.

| QBE facility | Current NPL emission/use | Consequence |
|---|---|---|
| `w`, `l`, integer arithmetic/comparison, branches, loads/stores | Used for tags, values, addresses, fixnum fast paths, and object fields | The backend already exposes common scalar operations to QBE optimization. |
| `s`, `d`, floating arithmetic and conversions | General numeric operations go through runtime; float literals store IEEE bits in static boxes | Unboxed floating-point specialization would require numeric type/representation analysis. |
| `data`, symbol relocations, `export` | Literals, datum graphs, globals, exported entry/storage symbols | Static vectors and bytevectors already use this facility. |
| `alloc8` | Argument buffers and local slots; hoisted where loops require it | Not a substitute for escaping heap objects. |
| Direct/indirect calls | Runtime functions, direct entries, closure entries, CPS trampoline requests | Proper tail behavior is implemented by NPL, not inferred from a QBE call. |
| Phi construction | Backend reassigns temporaries across branches/loops; QBE constructs SSA | Absence of explicit `phi` is supported IL, not a missing optimization by itself. |
| Aggregate arguments/results, nested types, fixed array fields, unions | No aggregate definitions or aggregate call ABI emission | Useful for an explicit C by-value FFI extension. |
| `thread data`, `thread $symbol` | Not emitted today | Useful for T1 current-context lookup; see the thread design. |
| C variadics (`...`, `vastart`, `vaarg`), special `env`, `blit` | Not emitted by the current backend | NPL rest arguments and closure environments use its own argc/argv and ordinary `l` parameter ABI. |
| SIMD, atomic operations | No corresponding general IL facilities in this QBE | Repeated aggregate fields are not SIMD; atomics still need runtime mechanisms. |

Emission sources: [state](../../src/compiler/backend/state.npl),
[program](../../src/compiler/backend/program.npl),
[expression](../../src/compiler/backend/expression.npl),
[tail](../../src/compiler/backend/tail.npl), and
[numeric](../../src/compiler/backend/numeric.npl).
The numeric source comment saying QBE data has no floating scalar kind is
inaccurate: the grammar and probe accept `d d_2.5`. The emitted `l` bits are
valid and preserve the captured IEEE representation; no behavior fix is needed.

## Records and C structs

NPL records use `BOX_RECORD` and the `NplVector` layout: a header, length and
capacity, a runtime type identity in slot zero, then tagged `NplVal` fields.
Constructors allocate through `npl_make_record`; the backend already inlines
guarded field loads/stores and the mutation write barrier. See
[record expansion](../../src/compiler/frontend/expand-record.npl),
[inline emission](../../src/compiler/backend/inline.npl),
[runtime representation](../../runtime/npl.h), and
[record runtime](../../runtime/value.c).

Adding `type :record = ...` alone changes neither allocation nor field access.
Stack allocation or scalar replacement requires proof about escape, identity,
mutation, GC visibility, and continuation lifetime. Typed unboxed fields need
a language/runtime representation decision; ordinary record fields accept
arbitrary values. Preserve the shared frontend and evaluator semantics.

`define-c-struct` is currently a descriptor for a foreign memory view.
[Layout probing](../../src/compiler/frontend/c-structs.npl) records `sizeof`
and requested fields' `offsetof`; field type codes come from declarations.
Declarations may expose only a subset of the actual fields. No complete
ordered member tree or alignment is recorded, and nested/array field types
are not supported. [Typed function signatures](../../src/compiler/frontend/c-functions.npl)
accept `struct:name` arguments but not struct results.
[C stubs](../../src/compiler/backend/c-stubs.npl) validate the struct view and
pass its address as `void *`; C handles the native ABI.

By-value FFI is a useful separate extension, not a reinterpretation of
`struct:name`. A direct QBE path needs complete ABI-relevant member types,
alignment, size, padding, nested/array layout, ownership of returned storage,
and target tests. Size and offsets alone do not determine register classes
for mixed integer/floating structs or homogeneous floating aggregates.
Opaque byte aggregates are not a universal replacement for that information.
Packed structs, bitfields, unions, and over-alignment need explicit supported
cases or a C bridge; do not infer support from the simple probe below.

The alternative is to extend the existing generated C stubs with real native
headers and by-value signatures, letting C compute the ABI. Compare this with
direct QBE emission before adding another FFI path. Both native compilation
and npli must keep the same validation, lifetime, and result semantics.

## Vectors, arrays, and static storage

[Literal emission](../../src/compiler/backend/data.npl) already creates
static vectors, bytevectors, strings, pairs, and shared/cyclic datum graphs.
For `'#(10 20)` and `'#u8(1 2 255)`, the observed output is:

```text
data $v_74 = { w 2, w 1, w 2, w 2, l 40, l 80 }
data $bv_75 = { w 6, w 1, w 3, w 3, b 1, b 2, b 255 }
```

The four header words precede tagged values or raw bytes. Fixnums use a
two-bit tag shift. Literal flags preserve immutability; fresh mutable vectors
still need fresh runtime storage. QBE `data` fields are packed without implicit
inter-field padding, whereas aggregate definitions compute layout alignment.

QBE `type :a = { w 100 }` describes a fixed memory layout. It provides no
dynamic length, bounds checks, GC tracing, element boxing, or SIMD operations.
`data $a = { z 400 }` provides static zeroed storage for 100 raw words; it does
not construct an NPL vector. Bulk `blit` has a constant byte count and cannot
replace arbitrary overlapping or GC-sensitive vector mutation without checks.

[NPL arrays](../../lib/npl/data/array.sld) are ordinary vectors/strings, shape
plus flat tagged data for literals, or records for general/shared arrays.
Shared arrays may use an arbitrary index-mapping procedure. Uniform prototypes
such as `ar64`/`au8` currently do not create packed native-element storage.
[FFI c-arrays](../../lib/npl/ffi.sld) already provide contiguous raw typed
storage, with explicit conversion to/from tagged vectors and foreign lifetime
checks. Reuse those conversion/memory mechanisms when investigating packed
arrays, while retaining managed-array ownership and GC semantics.

## Validation and limits

- Host macOS arm64, vendored `build/qbe/qbe`: C -> QBE -> C by-value
  argument/result round trips passed for `{ int; double; }`,
  `{ int[3]; double; }`, and `{ double[2]; }`. A static `{ w 17, z 4,
  d d_2.5 }` object matched C's field offsets and values. The C harness used
  `-std=c99 -Wall -Wextra -Werror`.
- The same aggregate IL emitted successfully for `amd64_sysv`, `arm64`, and
  `rv64`; those targets were not executed. Temporary source and harness:
  `/var/folders/2c/99km9vvs33xdxhbl_8mjdcv00000gp/T/npl-qbe-aggregate-uwabvprz/`.
  The core call pattern is shown below for reproduction.
- A small importless NPL record/vector/bytevector program emitted 534 IL
  lines, only `l`/`w` result temporaries, zero aggregate definitions, and
  runtime record constructor calls. QBE accepted the emitted IL. This is a
  concrete sample, not a whole-program feature-coverage percentage.
- Native QBE/C TLS read/write and pthread isolation passed for a static link
  and a startup-linked dylib. Harness: `/tmp/npl-tls-zsksmbrr/`. The probe
  defines `export thread data $probe_tls = { l 7 }`, accesses it through
  `thread $probe_tls`, and shares it with C `extern __thread int64_t probe_tls`.
  Two workers retain independent values and leave the main thread unchanged.
- `extern thread` emission passed on amd64_sysv and arm64_apple; arm64 ELF
  and rv64 reject it, consistent with their emitters. Linux execution and
  late `dlopen` were not tested. Temporary probes are disposable evidence,
  not permanent acceptance tests or claims of NPL threading support.

```text
type :mixed = { w, d }
export function :mixed $q_mixed(:mixed %in) {
@start
    %out =:mixed call $c_mixed(:mixed %in)
    ret %out
}
```

Here C declares `struct mixed { int n; double x; };` and implements
`struct mixed c_mixed(struct mixed v)`, with a matching declaration for
`q_mixed`. Both argument and result are by value in C; QBE IL uses addresses
of aggregate storage and lowers the platform ABI.

## Recommended order

1. Use the proven TLS/accessor path within [T1](../design/threads.md), with
   explicit roots, thread registration, and target/link-mode validation.
2. Measure record accessor and C field lookup costs before specialization;
   aggregate annotations alone have no demonstrated performance benefit.
3. Add by-value FFI only with an explicit surface contract and complete C
   layout evidence; compare extending the existing C bridge first.
4. Treat packed numeric arrays/unboxed arithmetic as a separate measured
   representation change, with native/evaluator differential fixtures.
