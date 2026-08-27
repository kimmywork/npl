# NPL — A Typed, Native Scheme with Neural-Network Constructs

> 设计方案 v1。语言暂名 **NPL**（Native Programmable Language）。本文是设计文档，配套 `PLAN.md`（增量执行计划）与项目骨架。
>
> **实现状态说明**：本文描述目标架构，不表示所有条目已经落地。当前证据基线为：
> bootstrap fixpoint、mmap/mark-sweep GC、macro 核心、managed closures、
> proper tail calls 与 escape-only `call/cc` 已完成；显式 Integer/Bool typed-unboxed、
> guards 和 Float typed lowering 已接通；后续完整类型、R7RS、库与语言能力以
> self-hosted compiler 为实现主体，stage0 只维持 bootstrap 所需子集；self-hosted
> Pair typed pointer 与 unary typed lambda/managed capture 已落地，HM、
> record/row/refinement 尚未完成；
> 完整数值塔、NN、网络和 target tier 仍未完成。逐阶段状态以 `PLAN.md` 为准。
> 本轮不实现并行调度与 structured concurrency。

---

## 1. 设计目标与约束

### 1.1 目标

| 目标 | 说明 |
|------|------|
| R7RS-small 兼容核心 | 实现 R7RS-small 的语义子集（tail call、continuation via CPS、hygienic macro、`call/cc` 限制形式、数值塔） |
| 类型化扩展 | 借鉴 Typed Racket：`(: x : Integer)` / `(define x : Integer 5)`，模块级类型注解，可选类型检查 |
| 类型推导 | 基于 Hindley–Milner (HM) 的子集 + row polymorphism for records，渐进式（untyped 代码退化为动态） |
| Native 编译 | 经 QBE → 汇编 → 链接，目标 arm64（Apple Silicon 优先）、amd64、rv64 |
| 自举 | 三阶段 bootstrap，最终用 NPL 自身实现 NPL 编译器 |
| 应用层库 | pthread/structured concurrency、network (socket)、文件 IO |
| 自动并行化 | v1：CPU SIMD 自动向量化（NEON/AVX）；GPU/MPS 后置到 v2 |
| Minimal | 不直接依赖 C runtime（最小 libc shim 或裸 syscall），二进制体积小 |
| 多后端预留 | 语言层 + IR 层设计为 wasm / GPU 后端留接口 |
| NN as language construct | 静态图 DSL（快速路径）+ 一等张量（灵活路径）分层支持 |

### 1.2 硬约束（已确认的用户决策）

- **GC**：Stop-the-world Mark-Sweep，自实现。
- **stage0 宿主**：Apple clang + C99（与 QBE 自身保持一致）。
- **NN**：静态图 DSL + 一等张量，分层。
- **GPU**：v1 仅 CPU SIMD（NEON/AVX），GPU/MPS 后置。
- **工具链**：CMake ≥ 3.20 + Ninja；Apple clang 17。

### 1.3 明确不做（v1）

- 不实现 R7RS `dynamic-environment`、`scheme` 命令的全部 REPL 交互子集。
- 不实现 `syntax-rules` 之外的全部 `syntax-case`（v1 仅 `syntax-rules` + `er/ir macros` 的受限形式）。
- 不实现 GPU/MPS/Metal 后端运行时（仅预留 IR 分层与 offload 接口）。
- 不实现 WASM 后端代码生成（仅保证 IR 可降级到 wasm-compatible 形式）。
- 不依赖 libc 的 `malloc`/`printf`/`pthread`（用最小 shim 或裸 syscall 替代；网络/线程库以 syscall 直实现或极薄封装）。

---

## 2. 语言规格

### 2.1 语法

R7RS-small 兼容的 S-expr 语法，额外引入：

```scheme
;; 类型注解（Typed-Racket 风格，前缀冒号位置）
(define x : Integer 5)
(: y : (-> Integer Integer))
(define (f [a : Integer]) : Integer (+ a 1))

;; 模块级类型定义
(:type Point (record (x Integer) (y Integer)))
(:type Color (enum red green blue))

;; 张量类型（一等值路径）
(:type (Tensor Integer (3 4)))
(define m : (Tensor Float (3 4)) (tensor ...))

;; 静态计算图（DSL 路径）
(nn-module
  (:layer dense ((in : Integer) (out : Integer))
    (define w : (Tensor Float (out in)) (randn))
    (define b : (Tensor Float (out))   (zeros))
    (define (forward [x : (Tensor Float (in))])
      (add (matmul w x) b))))

;; 并发原语
(spawn (lambda () (display "hi")))
(await (make-channel))
```

### 2.2 语义

| R7RS 特性 | NPL 处理 |
|-----------|----------|
| Proper tail call | 通过 CPS 变换 + QBE `jmp`（函数内循环）；跨函数尾调用用 env trampoline |
| `call/cc` | v1 仅支持 escape-only continuation（delimited，到最近 `call/cc` 边界），全 `call/cc` 后置 |
| 数值塔 | 固定大小整数（w/l）+ bignum（运行时库）+ 浮点（s/d） |
| hygienic macro | `syntax-rules`（v1），`syntax-case` 子集（v1.x） |
| record | 自定义聚合类型，映射到 QBE `type :foo = {...}` |
| 库 | `(import (npl base))` 等，模块即文件 |
| 尾位置 | 编译器静态标注 tail position，驱动 CPS/env 转换 |

### 2.3 类型系统

**双层**：runtime tag（动态值）+ static type（可选注解）。

- 静态层：HM + row polymorphism for records + refinement（受限，`[x : Integer (> 0)]`）。
- 注解缺失 → 退化为 `Dynamic`，编译为带 tag 的 boxed 值。
- 注解存在 → 编译为 unboxed 的 QBE base type（`w`/`l`/`s`/`d`），省去 tag 检查。
- 跨边界（typed↔untyped）：插入 cast/check 函数（"guards"），类比 Typed Racket 的行为契约。

类型推导算法（v1）：
1. 简单 HM（algorithm W）作用于带注解的模块。
2. 未注解的顶层绑定退化为 `Dynamic`。
3. 局部 `let` 推导，但跨模块边界必须注解或退化为 Dynamic。

### 2.4 张量与 NN 构造

**一等张量路径**（动态）：
- `(Tensor T shape)` 是一等值，运行时 shape 可变（动态 shape）。
- autograd：每个 Tensor 携带反向计算闭包链；`(backward t)` 遍历执行。
- 内存：dense row-major，`alloc16` 对齐到 16/32 字节（为 SIMD）。

**静态图 DSL 路径**（编译期）：
- `nn-module` 是编译期宏，展开为：
  - 一组 QBE `data` 定义（参数权重，全局符号）。
  - 一组 `export function` 实现 forward/backward。
  - 编译期静态 shape 推导，内联 SIMD kernel。
- 关键点：`forward` 在静态图下编译期被 specialize 成无 tag 的数值循环，QBE 再做向量化。

---

## 3. 编译器架构

### 3.1 Pass 流水线（Nanopass 风格，分阶段）

```
Source (S-expr)
  │
  ▼ Lexer + Reader          → TokenStream
  │
  ▼ Parser (S-expr→AST)      → L0 Core (AST)
  │
  ▼ Macro expansion         → L1 Expanded (syntax-rules)
  │
  ▼ Type check / inference  → L2 Typed (HM + annotations)
  │   └─ typed↔untyped guards insertion
  │
  ▼ A-normal form (ANF)     → L3 ANF
  │   └─ 显式 let, 命名所有中间值
  │
  ▼ CPS transform           → L4 CPS (continuation explicit)
  │   └─ tail call → jmp; escape continuation → env-captured closure
  │
  ▼ Closure conversion      → L5 Flat (闭包→env 参数 + 函数指针)
  │
  ▼ QBE IL generation       → *.ssa (QBE IL)
  │
  ▼ QBE → asm → obj        → *.o
  │
  ▼ link (minimal runtime) → executable
```

### 3.2 关键 pass 设计

**Macro expansion（L0→L1）**：R7RS `syntax-rules`，用展开器表（展开环境），hygiene 通过变量重命名实现。

**Type check（L1→L2）**：
- 对每个 `define`/`lambda` 收集类型签名（注解或推导）。
- HM algorithm W 处理 `let`/`lambda`。
- 生成 guards 函数用于 typed↔untyped 边界。

**CPS transform（L3→L4）**：
- 仅对需要 `call/cc` 的区域做完整 CPS。
- 其他代码：仅标注 tail position，编译时 tail call 直接映射为 QBE 的函数内 `jmp`（self-recursion）或 env-trampoline（跨函数）。
- escape-only continuation：实现为"带逃逸标记的异常"，编译为 QBE 的 `jmp` 到外层 block + env 标记检查。

**Closure conversion（L4→L5）**：
- 每个闭包 = (函数指针, env 指针) 对。
- env 是 `alloc16` 分配的 record，存自由变量。
- 调用编译为 QBE 的 `call $f(env %e, ...)`，定义编译为 `function w $f(env %e, ...)`。
- 这正是 QBE `env` 参数的原生用途（见 `external/qbe-1.3/doc/il.txt` ~ Functions 节）。

**QBE IL generation（L5→*.ssa）**：
- 非必需 SSA：直接用 `alloc4/alloc8` 栈变量 + store/load（参考 minic 与 cproc 风格），QBE 能编译非 SSA 代码（见 il.txt ~ Phi 节："QBE is able to fixup programs not in SSA form"）。
- typed unboxed 值 → QBE `w`/`l`/`s`/`d`。
- Dynamic boxed 值 → QBE `l`（指针）+ runtime tag struct。
- 全局 → `data $foo = { ... }`。
- record → `type :foo = { l, l, ... }`（传指针）。

### 3.3 运行时类型表示

```c
// Boxed value header（每个 Dynamic 值前 8 字节）
struct NplVal {
    uint32_t tag;     // TAG_INT | TAG_PAIR | TAG_STR | TAG_VEC | ...
    uint32_t flags;   // GC mark bit, frozen, etc.
    /* payload follows */
};

// tagged pointer optimization: 低位编码 tag，小整数/nil 直接内联到指针
//   xxx...x00 → fixnum (shift 2)
//   xxx...x01 → pointer to NplVal
//   ...10     → immediate (nil/bool/char)  (v1.x)
```

---

## 4. Runtime（minimal，不依赖 libc）

### 4.1 内存

- **堆分配**：`mmap` syscall（macOS: `__mmap` via libsyscall；linux: `syscall(SYS_mmap)`）。维护自己的 arena/heap，按 64KB chunk 增长。
- **栈分配**：QBE `alloc4/8/16`，由 QBE 管理栈帧。
- **GC**：stop-the-world mark-sweep。
  - Root set：寄存器（由 QBE emit 的 callee-save 约定）+ 栈帧扫描 + 全局 `data` 表。
  - Mark：DFS 遍历对象图，对象头有 `gc_bits`。
  - Sweep：free list，coalescing。
  - 触发点：`alloc` 检查阈值。

### 4.2 入口

- 不链接 libc 的 `_start`，自实现入口：
  - macOS：`main` 仍由 dyld 调用（macOS 无法完全脱离 dyld），但 main 内自举 runtime，不调用 libc 函数。
  - linux：可直接写 `_start`（汇编或 QBE `data` + `section ".text"`），不链接 crt。
- v1 采用混合策略：macOS 上链接但仅用 syscall shim；linux 上可做 freestanding。

### 4.3 线程（pthread 替代）

- **v1 minimal**：直接 `pthread_create` syscall 封装（macOS: `__pthread_create`；linux: `clone`），不依赖 libc pthread。
- **Structured concurrency**：语言层 `(spawn ...)` / `(await ...)` / `(scope ...)`，编译为：
  - `(spawn e)` → 创建线程 + 一个 promise channel。
  - `scope` 退出时 join 所有子任务，异常传播。
- v1 实现 thread-pool + work-stealing（简单版本）。

### 4.4 网络

- socket syscall 直实现（`socket`/`bind`/`listen`/`accept`/`connect`/`read`/`write`）。
- 提供 `(npl net)` 模块：`tcp-listen`、`tcp-accept`、`tcp-connect`、`read-line`/`write-string`。
- v1 阻塞 IO；async IO 后置（需 event loop + epoll/kqueue）。

---

## 5. 并行化与自动向量化（v1: CPU SIMD）

### 5.1 策略

- **自动向量化**：在 QBE IL 生成阶段，对已识别的"数值密集循环"（typed `(Tensor ...)` 上的 map/fold）生成 NEON（arm64）/ AVX（amd64）的 intrinsics-as-QBE-ops。
  - 由于 QBE IL 无 vector type，采用策略：**生成内联汇编块**（`asm` 不支持时）或 **调用预编译的 SIMD kernel 函数**（C 编写的 vectorized kernel，通过 QBE `call` 调用）。
  - v1 采用后者：用 Apple clang 编译一组 SIMD kernel（C + `__attribute__((target("neon")))`），NPL 生成 `call` 到这些 kernel。
- **并行化**（多核）：typed map over Tensor → 编译为 chunked parallel loop，分发到 thread-pool。

### 5.2 IR 层 offload 接口（为 GPU/wasm 预留）

在 L5（Flat）→ *.ssa 之间插入一个 **target tier**：

```
L5 Flat
  │
  ▼ target classification（CPU/GPU/wasm）
  │
  ├─ CPU path  → QBE IL → *.ssa → asm
  ├─ GPU path  → (v2) MLIR/SPV/Metal shader source
  └─ wasm path → (v2) wasm IL
```

- 每个 `nn-module`/`tensor` 操作标注 `(target cpu|gpu|wasm)`（默认 cpu）。
- CPU path 必须始终可用，保证 bootstrap 可行。

---

## 6. 自举（Bootstrap）

### 6.1 三阶段

| Stage | 实现 | 输入 | 输出 | 用什么编译 |
|-------|------|------|------|-----------|
| stage0 | C99（手写） | NPL 源 | QBE IL → asm | Apple clang |
| stage1 | NPL self-hosted compiler（由 stage0 编译） | NPL 源 | QBE IL → asm | stage0 |
| stage2 | stage1 编译器的再编译产物 | NPL 源 | QBE IL → asm | stage1 |

- stage2 应与 stage1 字节一致（参考 cproc 的 `bootstrap` target：`make bootstrap` 验证 stage2==stage3）。
- stage0 用 C99 写，因为：QBE 本身是 C99；cproc 也是 C99；保证有可靠宿主。

### 6.2 stage0 NPL 子集

stage0 支持的 NPL 子集只以“足以编译并运行 `src/compiler.npl`”为准：
- 保留 compiler.npl 实际使用的基础 forms、数据操作和尾调用；
- 类型注解只需传递 self-host compiler 所需的信息；
- 完整 R7RS、类型系统、库和语言扩展由 self-hosted compiler 实现；
- 不因目标语言最终功能而预先扩展 stage0；
- 无 macro 和 `call/cc`，因为 compiler.npl 不依赖它们。

### 6.3 bootstrap 验证

```
# 用 Apple clang 编译 stage0
clang stage0/*.c -o nplc0

# stage0 编译 stage1 NPL 源
./nplc0 src/compiler.npl -o nplc1

# stage1 自举
./nplc1 src/compiler.npl -o nplc2

# 验证 stage1==stage2（字节一致）
diff <(./nplc1 src/compiler.npl -o /dev/stdout) <(./nplc2 src/compiler.npl -o /dev/stdout)
```

---

## 7. 项目结构

```
npl/
├── AGENTS.md                  # agent 工作约定
├── DESIGN.md                  # 本文档
├── PLAN.md                    # 增量执行计划
├── CMakeLists.txt             # 顶层构建
├── external/
│   └── qbe-1.3/               # QBE 后端（已存在）
├── stage0/                    # C99 宿主编译器（bootstrap 起点）
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.c             # driver
│   │   ├── reader.c           # lexer + parser
│   │   ├── ast.h              # AST 定义
│   │   ├── type.c             # HM 类型检查
│   │   ├── cps.c              # CPS + closure conversion
│   │   ├── qbe_gen.c          # QBE IL 生成
│   │   └── runtime/
│   │       ├── gc.c           # mark-sweep
│   │       ├── alloc.c        # heap (mmap)
│   │       ├── thread.c       # pthread shim
│   │       ├── net.c          # socket shim
│   │       └── simd_kernels.c # NEON/AVX kernel
│   └── tests/
├── src/                       # NPL 自举源（stage1/2 用）
│   ├── compiler.npl           # 用 NPL 写的编译器
│   ├── base.npl               # (npl base) 库
│   ├── net.npl                 # (npl net)
│   ├── thread.npl              # structured concurrency
│   └── nn/
│       ├── tensor.npl         # 一等张量
│       └── module.npl         # 静态图 DSL
├── runtime/                   # 共享 runtime（C，被 stage0 和自举产物链接）
│   ├── CMakeLists.txt
│   ├── gc.h / gc.c
│   ├── alloc.h / alloc.c
│   ├── npl.h                  # 值表示 + tag
│   └── simd_kernels.h
├── tests/
│   ├── r7rs/                  # R7RS conformance 测试
│   ├── typed/                 # 类型系统测试
│   ├── nn/                    # 张量/NN 测试
│   └── bootstrap/             # 自举验证
└── tools/
    ├── run_tests.sh
    └── check_bootstrap.sh
```

---

## 8. 构建系统

- 顶层 CMake，子项目 `stage0`、`runtime`、`tests`。
- QBE 作为 external：CMake `ExternalProject_Add` 或直接用已构建的 `external/qbe-1.3/qbe`。
- Generator：Ninja。
- 编译器：`/usr/bin/clang`（Apple clang 17，arm64-apple-darwin）。
- 关键 CMake 选项：
  - `NPL_TARGET=arm64|amd64|rv64`（默认 arm64）
  - `NPL_BOOTSTRAP_STAGE=0|1|2`
  - `NPL_USE_LIBC=ON|OFF`（OFF 时纯 syscall，macOS 上仍链接 dyld）

---

## 9. 库设计（应用层）

### 9.1 `(npl base)` — R7RS base 子集 + 扩展
- pair/list/vector/string/map/for-each
- 数值塔（fixnum/flonum/bignum）
- record（`(define-record ...)`）

### 9.2 `(npl thread)` — structured concurrency
- `(spawn expr)` → thread + promise
- `(await promise)`
- `(scope body ...)` → 退出 join + 错误传播
- channel：`(make-channel)` / `(send ch v)` / `(recv ch)`
- 内部：thread-pool + work-stealing deque

### 9.3 `(npl net)` — 网络
- `(tcp-listen port)` → listener
- `(tcp-accept listener)` → connection
- `(tcp-connect host port)` → connection
- `(read-line conn)` / `(write-string conn s)`
- v1 阻塞；async 后置

### 9.4 `(npl nn tensor)` — 一等张量
- `(tensor ...)` 构造
- `(tensor-map f t)` / `(tensor-fold f init t)`
- `(matmul a b)` / `(add a b)` / `(scale s t)`
- autograd：`(backward t)` 遍历计算图

### 9.5 `(npl nn module)` — 静态图 DSL
- `nn-module` 宏 → 编译期生成 forward/backward 的 QBE `function`
- `:layer` 定义（参数化）
- 静态 shape 推导 + SIMD kernel 内联

---

## 10. 风险与权衡

| 风险 | 影响 | 缓解 |
|------|------|------|
| QBE 无 vector type，自动向量化受限 | SIMD 优化需走 kernel-call 路径 | 用预编译 C kernel，NPL 生成 call；长期考虑 fork QBE 加 vector type |
| 完全脱离 libc 在 macOS 上困难（dyld 依赖） | freestanding 目标在 macOS 受限 | 接受"链接但不用 libc 函数"的妥协；纯 freestanding 仅 linux 可行 |
| `call/cc` 全支持会阻碍 CPS 性能 | 全 call/cc 需完整 CPS，拖慢所有代码 | v1 仅 escape continuation；全 call/cc 后置 |
| 自举 stage0 巨大 | C99 写完整编译器量大 | 参考 minic（951 行实现 C→QBE 前端）与 cproc，控制范围 |
| HM + Dynamic 混合的 guard 开销 | 跨边界检查开销 | 提供 typed boundary 优化；profiling 驱动 |
| 三架构 ABI 差异 | arm64/amd64/rv64 测试矩阵大 | v1 主测 arm64（Apple Silicon），其余 best-effort |

---

## 11. 参考实现与先例

| 项目 | 关系 | 启示 |
|------|------|------|
| **QBE** (`external/qbe-1.3`) | 后端 | `env` 参数 = 闭包；非 SSA 友好；minic 是 951 行的 C→QBE 前端参考 |
| **cproc** (michaelforney/cproc) | C→QBE 先例 | 自举 stage1/2/3 + 字节一致验证；QBE 类型映射策略 |
| **Chez Scheme** | Scheme 自举先例 | nanopass 架构、分层 pass、自举成熟 |
| **Typed Racket** | 类型化 Scheme | guard 模型、渐进类型、契约边界 |
| **Cyclone / Loko** | R7RS native Scheme | 不依赖 libc 的 syscall 运行时、自有 GC |
| **Ribbit** | 极小 Scheme | 自举、嵌入式友好 |
| **Hare** (qbe backend) | 系统→QBE 语言 | 类型到 QBE base type 映射 |

证据来源（本地）：
- `external/qbe-1.3/doc/il.txt` — IL 规格、env/closure、phi 可选、非 SSA
- `external/qbe-1.3/doc/abi.txt` — arm64/amd64/rv64 ABI
- `external/qbe-1.3/minic/minic.y` (951 行) — C→QBE 前端参考
- `external/qbe-1.3/ops.h` — QBE 操作全集（arith/mem/cmp/conv/cast/call/phi/jmp）
- cproc README (https://github.com/michaelforney/cproc) — 自举目标、stage 验证
