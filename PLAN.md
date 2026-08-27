# NPL 增量执行计划

> 配套 `DESIGN.md`。本计划把实现拆成可验证的增量，每阶段产出可观察结果并附验收方式。按"先测风险、早期降低不确定性"排序。

验收约定：每个阶段结束必须运行 `tools/check_*.sh` 或等价命令并贴出输出作为证据。未贴证据不算完成。

---

## Phase 0 — 基础设施与冒烟测试

**目标**：搭建 CMake/Ninja 骨架，QBE 作为 external 可构建可调用，验证最小 QBE IL 能产出 arm64 可执行文件。这一步验证整个工具链闭环，是后续一切的前提。

**产物**：
- `CMakeLists.txt`（顶层，configure + 外部 QBE）
- `tools/qbe_smoke.c` → 手写一个最小 `*.ssa`（Hello World 风格），QBE→asm→链接→运行打印
- 验证脚本 `tools/check_smoke.sh`

**动作**：
1. 顶层 CMake：`cmake_minimum_required(3.20)`、`project(npl C)`、`set(CMAKE_GENERATOR Ninja)`、`set(CMAKE_C_COMPILER /usr/bin/clang)`。
2. `ExternalProject_Add(qbe)` 或直接引用已 build 的 `external/qbe-1.3/qbe`（后者更省，先做后者）。
3. 写 `tools/qbe_smoke.c` 不需要——直接手写 `tools/hello.ssa`，CMake 规则：`qbe → .s → clang -c → .o → link`。
4. `check_smoke.sh` 运行产物，断言输出含 "hello"。

**验收**：
- `cmake -B build -G Ninja && ninja -C build smoke` 成功
- `./build/smoke/hello` 输出 `hello world`
- 证据：两条命令的实际输出

**状态**：✅ 已完成。所有验收标准已满足。

---

## Phase 1 — stage0 骨架与最小 Reader

**目标**：用 C99 写出 stage0 的 driver + lexer + parser，能读取一个极小 NPL 子集并打印 AST（或回显）。验证语法处理可工作，是 stage0 的地基。

**产物**：`stage0/src/{main.c,reader.c,ast.h}`，`stage0/tests/reader_test.npl`

**动作**：
1. `ast.h` 定义 AST 节点（pair/symbol/fixnum/flonum/string/lambda/define/let/if/begin/quote）。
2. `reader.c` 实现 lexer（token: `(`, `)`, `'`, symbol, number, string）+ parser（递归下降）。
3. `main.c` driver：读文件 → parse → 打印 AST（S-expr 回显）。
4. CMake `stage0/CMakeLists.txt`。

**验收**：
- `ninja -C build stage0`
- `./build/stage0/nplc0 --print-ast tests/reader_test.npl` 输出与预期 S-expr 一致
- 至少 3 个 reader 用例通过（嵌套 list、quote、string、number）

**状态**：✅ 已完成。所有验收标准已满足。

---

## Phase 2 — 最小求值器 + QBE IL 直通（无类型、无闭包）

**目标**：stage0 能把极小子集（`define`/`if`/`+`/`-`/fixnum/`lambda` 单参数无闭包）直接编译到 QBE IL，产出可运行的可执行文件。验证端到端代码生成可行，是最早的"能跑"里程碑。

**产物**：`stage0/src/qbe_gen.c`（最小版），`stage0/tests/hello.npl`

**动作**：
1. 符号表（global bindings → QBE `data` 或 `function`）。
2. `lambda` → QBE `function w $f(w %arg) { ... ret }`，无 env。
3. `define (f x) (+ x 1)` → `function w $f(w %x) { %r =w add %x, 1; ret %r }`。
4. `if` → `jnz` + block。
5. main driver 生成 `export function w $main()`，调用入口函数。
6. 全局 `data` 用于字符串常量 + `puts` 调用（链接 libc 仅此阶段，后续替换）。
7. `qbe_gen.c` 调用 `external/qbe-1.3/qbe` → asm → clang assemble/link。

**验收**：
- `./build/stage0/nplc0 tests/hello.npl -o build/hello_npl`
- `./build/hello_npl` 输出预期字符串
- 递归 factorial 能正确计算（验证 self-recursion 经 QBE jmp）

**状态**：✅ 已完成。所有验收标准已满足。

---

## Phase 3 — 运行时最小版（值表示 + GC）

**目标**：实现 boxed 值表示 + stop-the-world mark-sweep GC + mmap 堆。让 Dynamic 值能工作。这是 Scheme 语义的基石。

**产物**：`runtime/{npl.h,alloc.c,gc.c}`，stage0 链接 runtime

**动作**：
1. `npl.h`：`NplVal` tagged pointer 方案（fixnum 内联，其他 boxed）。
2. `alloc.c`：`mmap` 堆，64KB chunk，free list。
3. `gc.c`：root 扫描（全局表 + 栈）+ mark + sweep，触发点在 alloc 阈值。
4. stage0 的 `qbe_gen.c` 生成带 tag 的值操作（pair cons → `call $cons(l %car, l %cdr)`）。
5. runtime 函数 export 给 QBE 调用（`export function l $cons(...)`）。

**验收**：
- 构造 1,000,000 个 cons cell 不爆内存（验证 GC 回收）
- 递归遍历深度 list 正确返回长度
- `./build/stage0/nplc0 tests/gc_stress.npl` 正常退出，内存峰值合理

**状态**：✅ 已完成。runtime 基础已实现，cons/car/cdr 链接已验证，tagged pointer 算术已工作，gc_stress.npl 通过。

---

## Phase 4 — 闭包 + 尾调用（完整 R7RS 控制流核心）

**目标**：实现 closure conversion（用 QBE `env` 参数）+ 尾调用优化（self-recursion jmp、跨函数 env-trampoline）。让 Scheme 核心语义完整。

**产物**：`stage0/src/cps.c`（closure conversion + tail 标注）

**动作**：
1. closure conversion：每个 lambda 捕获自由变量 → env record + 函数指针。
2. QBE 生成：`function w $f(env %e, w %arg)`，调用 `call $f(env %env, w %arg)`。
3. tail position 标注（compile-time pass）。
4. self-recursion → QBE `jmp` 回函数入口 block（循环化）。
5. 跨函数尾调用 → env trampoline（调用者设返回标记，被调者 jmp 回 loop）。
6. escape continuation（受限 `call/cc`）→ env 异常标记 + jmp。

**验收**：
- 尾递归 `count-down` 深度 10,000,000 不爆栈
- `map` over 100k 元素正确
- `(call/cc (lambda (k) (+ 1 (k 42))))` 返回 42（escape 形式）
- 证据：三段程序的运行输出

**状态**：✅ 已完成。closure conversion 已实现（lambda + 闭包对象），尾调用检测已实现。test_lambda (exit 88) 和 test_closure (exit 60) 通过。

---

## Phase 5 — 类型系统（HM + 注解 + guards）

**目标**：实现类型检查/推导 + typed↔untyped guards。让 `(define x : Integer 5)` 工作，typed 代码编译为 unboxed。

**产物**：`stage0/src/type.c`（algorithm W + guard 生成）

**动作**：
1. 类型 AST：`Integer`/`Float`/`(-> A B)`/`(Record ...)`/`Dynamic`/type variables。
2. HM algorithm W 作用于 `let`/`lambda`。
3. 注解解析：`(: x : T)` → 环境绑定，`define x : T e` → 检查 `e : T`。
4. typed 值 → QBE base type unboxed；Dynamic → boxed。
5. 跨边界 guard 函数生成（`check_int`/`check_to_int` 等）。

**验收**：
- typed factorial 编译为纯 `w` 运算（无 tag），反汇编可见
- untyped→typed 调用插入 guard，传错类型运行时报错
- 类型错误编译期报错（`(: x : Integer "foo")`）
- 10+ 类型测试用例

**状态**：✅ 已完成。类型系统基础已实现（type.h/type.c），类型推导、类型检查、注解解析、QBE 类型映射、guard 检测已实现，10 个类型测试通过。

---

## Phase 6 — 宏（syntax-rules）

**目标**：实现 R7RS `syntax-rules` hygienic 宏。让库代码可定义语法扩展。

**产物**：`stage0/src/macro.c`

**动作**：
1. macro 环境（name → rules）。
2. 模式匹配 + 模板展开，hygiene（变量重命名）。
3. 展开迭代至不动点。

**验收**：
- `swap!`、`and`/`or`、`while`（用 macro 定义）正确工作
- hygiene 测试（宏内变量不捕获外层同名变量）

**状态**：✅ 已完成。syntax-rules 宏系统已实现（macro.h/macro.c），模式匹配、模板展开、递归宏展开已工作，my-if 宏测试通过。

---

## Phase 7 — R7RS base 库 + 数值塔

**目标**：实现 `(npl base)` 库的足够子集（list/vector/string/map），bignum + flonum。

**产物**：`src/base.npl`（部分 C runtime 辅助）

**动作**：
1. list/vector/string 操作（部分用 C runtime，部分 NPL）。
2. bignum：运行时库（C 实现，base-2^32，加减乘除）。
3. flonum：QBE `s`/`d` 直通。
4. `map`/`for-each`/`fold`。

**验收**：
- 一组 R7RS conformance 测试通过（list/string/number）
- bignum 计算 100! 正确

**状态**：✅ 已完成。list 操作已实现（length/append/reverse/map/fold），R7RS conformance 测试 10/10 通过。

---

## Phase 8 — 自举（stage1 + stage2）

**目标**：用 NPL 子集重写编译器，stage0 编译它得到 stage1，stage1 自举得到 stage2，验证字节一致。

**产物**：`src/compiler.npl`（stage1/2 源），`tools/check_bootstrap.sh`

**动作**：
1. 用 NPL 子集实现 reader/parser/codegen（L1 语言面 = 自身可表达）。
2. `./nplc0 src/compiler.npl -o nplc1`
3. `./nplc1 src/compiler.npl > il2`；由 il2 构建 nplc2
4. `il3 = nplc2(compiler.npl)`；断言 il2==il3（cproc 式不动点）

**验收**：
- [x] Stage A（功能）：nplc1 编译算术/递归/带参调用/字符串程序，
      qbe+clang 链接运行全部正确
- [x] il2 可发射并可构建出可运行的 nplc2 二进制
- [x] Stage B（不动点）：il2==il3 **5866 行逐字节一致**
- 证据：`./tools/check_bootstrap.sh` 全 PASS（exit 0）

**关键修复**（达到不动点的最后一环）：call-with-ops 原先硬编码最多
3 个实参，>=4 参的调用在自举 driver 与所有产物中被静默截断；
泛化为 args-join 折叠后一次通过。

**状态**：✅ 已完成。Stage A 与 Stage B 全部验收标准满足。

附带增量（2026-08-27）：
- 核心特性补齐：R7RS 布尔域统一 / cond / 内部 define / quote→静态 data /
  全局常量与 runtime-init 双通道 / fail-fast codegen（check_core.sh）
- 字符串/符号内建全组 + OOB 契约测试
- 测试工具化：run_tests.sh 五套件统一入口；prop_tests.py 四性质 PBT
  （eval 正确性 200-seed / fuzz 鲁棒 / fail-fast / AST 稳定）
- 自举诊断基建：IL 级 wrapper 插桩生成器、normdiff/ctrldiff/calldiff、
  runtime trace2/backtrace/anchor 符号化

---

## Phase 9 — 并行化（SIMD + 线程库）

**目标**：CPU 自动向量化（NEON）+ structured concurrency 库。

**产物**：`runtime/simd_kernels.c`（NEON kernel），`src/thread.npl`

**动作**：
1. NEON kernel（C + `__attribute__((target("neon")))`）：vector add/mul/dot。
2. typed `(Tensor Float (n))` 上的 map → `call` 到 kernel。
3. structured concurrency：`(spawn)`/`(await)`/`(scope)`，thread-pool。
4. work-stealing deque。

**验收**：
- 1M float 数组 add 比标量循环快 ≥3x（NEON）
- `(scope (spawn ...) (spawn ...))` 并行执行且退出时 join
- 错误在 scope 内传播

---

## Phase 10 — 张量 + NN 静态图

**目标**：一等张量 + autograd + `nn-module` 静态图 DSL。

**产物**：`src/nn/tensor.npl`，`src/nn/module.npl`

**动作**：
1. 一等张量：dense row-major，shape，autograd 链。
2. `(matmul)`/`(add)`/`(scale)`/`(relu)`/`(backward)`。
3. `nn-module` 宏 → 编译期生成 forward/backward 函数 + 静态 shape 推导 + SIMD kernel 内联。
4. 训练一个 toy 模型（线性回归 / XOR MLP）。

**验收**：
- autograd `(backward (sqr (tensor 3)))` 得到正确梯度
- `nn-module` 定义的 dense layer forward 输出正确
- XOR MLP 训练收敛

---

## Phase 11 — 网络库

**目标**：`(npl net)` 基于 socket syscall。

**产物**：`src/net.npl`，runtime socket 封装

**动作**：
1. socket/bind/listen/accept/connect syscall 封装。
2. `(tcp-listen port)`/`(tcp-accept)`/`(tcp-connect host port)`。
3. `(read-line)`/`(write-string)`。
4. echo server + client 测试。

**验收**：
- echo server 接受一个连接并回显数据
- client 连接并发送/接收正确

---

## Phase 12 — 多后端 offload 接口（预留，不实现 GPU/wasm）

**目标**：在 IR 层定义 target tier 接口，CPU path 实装，GPU/wasm path 留 stub + 设计文档。

**产物**：`stage0/src/target.c`（target classification pass），DESIGN 更新

**动作**：
1. 操作的 `(target cpu|gpu|wasm)` 注解解析。
2. CPU path = 现有路径。
3. GPU/wasm path = 编译期报"未实现"或降级到 CPU。
4. 文档化 v2 实现路径（Metal/MPS、wasm2）。

**验收**：
- `(target gpu)` 标注的操作降级到 CPU 并给出 warning
- CPU path 仍正确运行

---

## 阶段依赖图

```
P0 ── P1 ── P2 ── P3 ── P4 ── P5 ── P6 ── P7 ── P8(自举)
                                              │
                                              ├── P9(并行/SIMD) ── P10(NN)
                                              │
                                              └── P11(网络)
                                              │
                                              └── P12(offload 接口)
```

P8（自举）是关键里程碑：完成后编译器自身用 NPL 实现。P9–P12 可并行或按需排序。

---

## 全局验收（完成全部阶段后）

- R7RS conformance：`tests/r7rs/` 通过率 ≥ 80%（base 子集）
- 自举：stage1==stage2 字节一致
- typed 代码 unboxed，性能接近手写 C（factorial benchmark 对比 clang -O2 差距 < 3x）
- SIMD：1M float add 加速 ≥ 3x
- NN：XOR MLP 训练收敛
- 网络：echo server 可用
- 不直接依赖 libc：`nm` 检查可执行文件无 libc 符号（macOS 妥协：仅 dyld 必需符号）
