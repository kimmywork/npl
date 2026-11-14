# NPL 仓库全面 Code Review 与语言演进路线图

- **仓库**: `kimmywork/npl` @ `master` (`c05dd68`)，版本 0.1.1（0.1.2 SRFI 计划中）
- **性质**: 只读 review 与分析，未对仓库做任何修改、提交或推送
- **日期**: 2026-11-12
- **方法**: 通读编译器前端/后端、运行时 C 源码、库、REPL、构建与测试脚本；在本沙箱内实际重建工具链并运行测试门禁（Guile 依赖的档位除外）；针对可疑语义编写差分探针程序实测

---

## 1. 执行摘要

NPL 是一个自举的 R7RS-small Scheme 实现：共享一个宏展开前端，向下分出两条执行路径——`nplc`（整程序 CPS 变换 → QBE SSA → 原生代码）与 `npli`（求值器/REPL，把用户代码编译为 direct-style 原生闭包）。另有冻结的 stage0/bootstrap 链保证自举可复现。

**总体评价：代码质量显著高于同规模个人/小团队项目的平均水平。** 运行时 C 代码的 GC 根注册纪律、写屏障、有界比较/哈希预算、溢出检查高度一致；编译器各 pass 职责清晰、文件规模自律（≤800 行）；测试基础设施（56 档门禁、45 个三路 fixture、负向检查、GC 验证模式、属性测试）和文档纪律（AGENTS.md / workstate / conformance 文档）堪称典范。

**但本次 review 发现了一个此前未被记录的高严重度语义分歧**：npli 求值路径与 nplc 编译路径在 continuation 的跨顶层 form 重入行为上不一致（详见 F1）。这直接违反项目自己在 AGENTS.md 中定下的"双路径行为必须差分一致"规则，且 R7RS 要求 continuation 可重入。建议将其列为 0.1.x 的最高优先级修复项。

**验证结果**：本沙箱内（gcc + Unix Makefiles，无 Guile）——除 Guile 依赖档位（三路 fixture 的 Guile 通道、chibi 套件、数值对照、bench）无法运行外，其余门禁全部通过；`check_repl.sh` 在沙箱中偶发失败（重跑通过，判定为环境问题而非代码缺陷）。

---

## 2. 仓库概览

| 部分 | 规模 | 说明 |
|---|---:|---|
| `src/compiler/**/*.npl` | ~14,550 行 | 自举编译器：前端（lexer/expand/syntax-rules/library/control CPS）+ 后端（QBE SSA 发射） |
| `runtime/*.c` | ~9,325 行 | C99 运行时：保守分代 GC、trampoline/tail call、wind/handler/参数、哈希表、FFI、IO |
| `lib/**/*.scm/.sld` | ~6,340 行 | R7RS 标准库 + `(npl ...)` 库 + SRFI（125/1/111/196/189 等，0.1.2 扩充中） |
| `stage0/src/*.c` | ~3,290 行 | 冻结的 C 版 stage0（含 QBE 生成），永久不可修改 |
| `tests/` | 45 个三路 fixture | native / npli / Guile 三方字节级一致 |
| `tools/` | 56 档门禁 | `run_tests.sh` 等 check 脚本族 |

架构要点（与 review 结论相关）：

- **一条前端、两条后端**：`expand.npl`（卫生展开/alpha 重命名，700 行）、`syntax.npl`（带 marks 的 syntax-rules 匹配器，774 行）、`control.npl`（整程序 CPS 变换，792 行）是两条路径共享的核心；`library.npl`（777 行）实现 R7RS 库系统（only/except/prefix/rename）。
- **CPS + 堆上 continuation**：编译路径中每个闭包调用都在尾位置、带显式 continuation 参数；`call/cc` 在 `control.npl:378` 附近被特化为捕获当前 CPS continuation 的堆闭包（`npl_wind_transfer` 恢复 wind/handler），因此是**完全可重入的**。
- **direct-entry 优化**：非逃逸函数走 C 栈直接调用（`stack.c` 栈窗限制），逃逸则回落到堆 CPS/trampoline。
- **求值器**（`lib/scheme/internal/evaluator-compile.scm`，580 行）：把用户代码编译为 direct-style 的原生闭包（`lambda (frame) ...`），复用共享前端的展开结果；控制算子"用原生那套"。
- **GC**：`gc.c`+`alloc.c`（~1000 行）——保守 mark-sweep + 分代 nursery（bump 分配、young log、remembered set、写屏障），非移动式（因此地址哈希表无需 rehash），支持 `NPL_GC_VERIFY` 毒化模式。

---

## 3. 发现清单（按严重度）

### F1【高 · 语义分歧 + 未定义行为】npli 与 nplc 的 continuation 跨顶层 form 重入行为不一致

**现象**（全部在本沙箱全新克隆上复现）：

探针 `/tmp/cc5.scm`（等价于经典 call/cc 重入测试）：

```scheme
(define k1 #f)
(define n 0)
(define (f) (+ 1 (call/cc (lambda (k) (set! k1 k) 0))))
(write (f)) (write 'NL1) (newline)
(set! n (+ n 1))
(write (list 'pass n)) (newline)
(if (< n 3) (k1 10) 'done)
(write (list 'after n)) (newline)
```

| 路径 | 输出 | 判定 |
|---|---|---|
| nplc 原生 | `1NL1 / (pass 1) / 11NL1 / (pass 2) / 11NL1 / (pass 3) / (after 3)` | ✅ 正确的 R7RS 可重入语义 |
| npli 求值 | `1NL1 / (pass 1) / 11(after 1)` | ❌ 重入后跳过了 `'NL1`、`(newline)`、`(set! n ...)`、`(pass ...)`，n 未递增 |

探针 `/tmp/cc6.scm`（捕获后正常返回，再于后续顶层 form 调用 `(k1 42)`）：

- nplc 原生：无限循环（`captured/before-invoke` 反复打印）——这正是 R7RS 语义下该程序的**正确**行为；
- npli：静默地把 `42` 作为该 form 的回显值打印后正常继续并退出（exit 0）。**既没有重入，也没有触发 `runtime/trampoline.c:288-291` 中写明的 "continuation: invoked outside its dynamic extent" 错误路径**——行为落在两条设计意图之外。

**边界确认**：在单个顶层 form 内部（如 cc8：全部逻辑包在一个过程里），npli 的 call/cc 重入**是正确的**（`(1 11 11)`，与原生一致）。分歧仅出现在"form A 中捕获、form B 中调用"的场景——即 npli 的顶层 form 是逐个独立求值的，捕获的 continuation 不跨越 form 边界存活。

**根因定位**（读码结论）：

- 编译路径：`control.npl:338-385`（`control-captured-continuation` / `control-cps-call-cc`）把 call/cc 特化为捕获 CPS continuation 的堆闭包，整程序共享一条 continuation 链，顶层 form 串联其间——所以可重入。
- 求值路径：`evaluator-compile.scm` 把用户代码编译为 direct-style 原生闭包，`call/cc` 以一等值绑定到编译进 npli 的控制算子包装（SSA 中可见其形态为"捕获当前 wind/handler 并构造 continuation 闭包"的包装函数）。该包装的 CPS-ABI continuation 与求值器的 direct-style 调用帧不组合；超出动态范围后唯一可用机制是 `runtime/trampoline.c:272-319` 的 escape-only longjmp 帧（`NPL_MAX_ESCAPE_DEPTH 64`），而帧弹出后 `g_escape_frames` 中的陈旧 `jmp_buf` 与 id 匹配逻辑使得越界调用行为未定义（cc6 证明它既不重入也不报错）。

**影响**：任何在 REPL/eval 中"捕获 continuation、稍后调用"的代码（生成器、协程、测试装置、backtracking）在 npli 下静默产生错误结果而非报错。这违反 AGENTS.md 的双路径差分一致规则；`docs/r7rs-conformance.md` 的 Known deviations 清单未记录此项。README 第 17 行宣称 "first-class continuations"，对 npli 路径而言言过其实。

**建议**（供后续参考，本次未改动）：
1. 最小修复：在 `escape_continuation`（trampoline.c:275）的 id 搜索未命中时，把 `exit(1)` 改为抛出可捕获的 `npl_primitive_error`，并让求值器在 form 求值结束后使该 form 捕获的 escape 帧失效（弹出时清除 id），保证越界调用成为确定性错误而非 UB。
2. 正式修复方向：让 npli 的每个顶层 form 也走 CPS 边界（例如用 `npl_call_sync`/trampoline 提供的 continuation 链把 form 串起来），或明确文档化"npli 中 continuation 不跨顶层 form 存活"并加差分 fixture 固化该限制。
3. 无论选哪条，都应补一个跨 form 重入的三路差分 fixture（Guile 通道可作 oracle），并更新 conformance 文档。

### F2【中 · 文档与实现不符】"first-class continuations" 的适用范围未文档化

`README.md:17` 宣称 first-class continuations；实际：
- 编译路径：完全可重入（本次实测确认）；
- 跨 C 帧（FFI 回调、原语内部、`npl_call_sync`）只有 escape-only longjmp 帧，深度上限 64（`trampoline.c:263`），越界调用当前行为见 F1；
- `docs/r7rs-conformance.md` 的 Known deviations 一节没有记录任何 continuation 限制。

建议在 conformance 文档补一条已知限制，并说明 escape 深度上限。

### F3【中 · 内存安全】record 访问器缺少边界检查

`runtime/value.c:71-83`：`npl_record_ref`/`npl_record_set` 直接 `record->data[npl_fixnum_value(index)]`，无负数/上界检查（注释承认依赖展开期校验索引）。任何绕过展开期校验的路径（FFI、未来在 eval 中直接调用原语、或展开器回归）都会变成越界读写。与运行时其他地方的 `require_*` 防御风格不一致。建议加 `index < record->size` 检查并抛可捕获错误（热路径可由编译器证明安全后省略）。

### F4【中 · 错误处理不一致】`fprintf+exit(1)` 与可捕获错误混用

`error.c` 把 `exit(1)` 定位为"内部不变量失败"，但若干 `exit(1)` 点可从用户程序触达，例如：
- `trampoline.c:299` call/cc 嵌套超 64 层；
- `trampoline.c:290-291` continuation 越界调用（且如 F1 所述实际未触发）；
- `control.c` 若干 wind/handler 不变量点；`value.c` 的 record 路径（F3 相关）。

用户可触达的失败应统一走 `npl_primitive_error`（可被 `guard`/`with-exception-handler` 捕获），仅真正的运行时不变量才允许 abort。建议做一次专项清点。

### F5【低 · 性能】读取器字符串拼接为 O(n²)

`src/compiler/reader/lexer.npl:245-286`：`lex-string` 对每个字符执行 `(string-append out (int->string code))`，长字符串字面量（大文件、base64 内嵌数据）为二次方复杂度。建议改为累积 char list / buffer 一次性 `list->string`。

### F6【低 · 健壮性】`npl_read_file` 不区分短读与错误

`runtime/io.c:92-93`：`fread` 返回值 `got` 只用于截断，磁盘错误导致的短读会被当作部分内容静默返回。建议 `got != (size_t)size` 时报 file error。

### F7【低 · 可移植性】FFI 函数地址打包进 fixnum

`runtime/ffi.c`：函数指针经 `(int64_t)(uintptr_t)address` 打包为 fixnum。当前 47/48 位虚拟地址下安全（注释已承认），但在更宽的地址空间或压缩指针布局下会溢出。建议改用专门的 foreign 对象类型或至少加运行时断言。

### F8【低 · 理论】syntax-rules pattern 键的分隔符碰撞

`src/compiler/frontend/syntax.npl` 的 `pattern-key` 用 `(int->string 1)`（0x01）拼接"名字+mark"。含 0x01 字节的 `|...|` 符号理论上可碰撞。实际风险极低（reader 可以产出该字节），建议换成结构化键（pair/向量）一劳永逸。

### F9【信息 · 已知 P1】诊断能力：无源码行列、无栈回溯

宏展开/求值错误只有消息文本；仅括号匹配跟踪行列（`src/compiler/reader/lexer.npl` 的 `check-parens`）。`.agents/workstate.md` 已把 "source spans and stack traces" 列为遗留 P1，本 review 确认其必要性：以 F1 为例，若展开错误带位置，此类问题的定位成本会大幅下降。

### F10【信息 · 环境】测试基础设施在本沙箱的两个偶发问题

1. `check_repl.sh` 两次失败（exit 1、日志为空）、两次重跑通过；手工复现其 `persistent.err` 比对内容完全一致。判断为构建目录清理竞态类环境问题，非代码缺陷。建议脚本内对日志为空的失败输出诊断信息。
2. gcc + Unix Makefiles 并行构建时出现一次 `.size expression for _lambda_1837 does not evaluate to a constant` 汇编错误，重跑即消失。疑似 QBE 输出与并行汇编的偶发交互，值得在 CI 中留意（CI 用 clang + Ninja 未复现）。

### 正面评价（同样重要）

- **GC 纪律**：所有分配点一致地 root/unroot、所有 old→young 写入过写屏障（`alloc.c`/`gc.c` 全文核验）；`NPL_GC_VERIFY` 毒化模式是少见的自觉设计。
- **有界性思维**：`equal?`/`equal_hash` 对循环结构有预算上限（`hashtable.c`），`list?` 用快慢指针判环（`base.sld:94`），字面量冻结（`npl_freeze_literal`）落实 R7RS 3.4。
- **自举工程**：冻结 bootstrap + seed relay（gen1==gen2==gen3，6,753 行 SSA 不动点）+ `check_stage0*.sh`，把最危险的环节变成了可机械验证的等式。
- **负向测试**：77 条负向检查 + 41/45 个三路 fixture（native/npli/Guile 字节级一致）正是能发现 F1 这类分歧的机制——只是恰好没有覆盖"跨顶层 form 重入"这个用例。
- **文档纪律**：AGENTS.md 的规则（NPL-first、800 行上限、冻结链、worklog）与实际代码状态完全吻合，罕见。

---

## 4. 分领域评述

### 4.1 编译器前端
`expand.npl`/`syntax.npl` 的卫生宏实现（整数 mark、mark 感知环境层、`bound-identifier=?`、拼接别名）正确且测试充分；`control.npl` 的 CPS 变换把 if 的 continuation 复制进两个分支并显式处理多值契约（`tests/contracts/continuation-*.npl`）。主要短板：无源码位置贯穿（F9）、字面量数字作调用头只给内部错误消息（conformance 文档已记录）、`lex-string` O(n²)（F5）。

### 4.2 编译器后端
`expression.npl`（QBE SSA 发射）、`tail.npl`（尾位置分析）、`environment.npl`（flat closure + cell 分配）、`program.npl`（顶层注册/直接入口）结构清晰，原语值包装的符号命名不依赖行序（`primitive-values.npl:16-23`）保证了 SSA 稳定性——这是 seed relay 能成立的前提，设计到位。`call/cc` 作为一等值的包装发射（本次 SSA 级核验）与求值器的衔接是 F1 的病灶所在。

### 4.3 运行时
除 F1/F3/F4/F6/F7 外无其他发现。`control.c` 的 wind 帧全转移矩阵（before/after 在重入、raise、escape 下的次序）有专门测试；`hashtable.c` 五种表类型 + 有界 equal 预算实现完整；`stack.c` 的栈窗 + 回落策略是 tail call 性能与正确性的关键，注释充分。

### 4.4 库
`(scheme base)` 手写部分风格统一、注释解释"为什么"而非"是什么"；`(npl hashtable)`（SRFI 125 全量）是 0.1.2 SRFI train 的良好开端。缺口（conformance 文档已列）：`sort`、`string-split/join/trim`、sets、comparators（SRFI 128——hashtable 的错误消息已引用它）、weak 引用（hashtable 忽略 weak-keys）。

### 4.5 REPL / 求值器
`src/repl/main.npl` + `lib/scheme/internal/*` 的设计（求值代码与编译代码自由互调、库按 checksum 匹配原生绑定）聪明且自洽；F1 是该设计唯一的已知裂缝。REPL 回显每个 form 的值（本次实测确认），错误以 R7RS error object 文本呈现，与编译程序一致。

### 4.6 构建 / 测试 / CI
CMake ≥3.20 + vendored QBE 1.3，`tools/build_toolchain.sh` 一键构建；56 档门禁覆盖 stage0 属性测试、GC 验证、驱动、安装、自举不动点。CI 仅 Linux（workstate 已记录 macOS 门禁本地跑通）。缺口：无 sanitizer/ASan/UBSan 档位、无模糊测试、Guile 依赖使部分档位在无 Guile 环境静默跳过（本沙箱即如此）。

---

## 5. 语言演进路线图

原则：延续项目现有节奏——每一步都先有设计文档、有三路差分 fixture、不破坏冻结链。以下按"修复 → 补强 → 现代化 → 安全 → 工程"分五个阶段，条目均标注依据（本次 review 的发现编号 / conformance 文档 / workstate 已有计划）。

### 阶段 0（0.1.x 收尾，随 0.1.2/0.1.3）——正确性优先

| 项 | 内容 | 依据 |
|---|---|---|
| 0-1 | **修复 F1**：npli continuation 跨 form 行为（最小修复=确定性错误；正式修复=CPS 边界串联顶层 form），补跨 form 重入差分 fixture | F1，最高优先级 |
| 0-2 | escape continuation 越界调用改为可捕获错误；弹出帧时清除 id | F1/F4 |
| 0-3 | 文档：continuation 限制（escape 深度 64、跨 C 帧 escape-only）写入 conformance Known deviations | F2 |
| 0-4 | 0.1.2 SRFI train 按既有计划推进（SRFI 1/13/130/140/133/19/170 + 数据结构 SRFI；18/230/226 → 0.1.3） | workstate |
| 0-5 | `npl_record_ref/set` 边界检查 | F3 |
| 0-6 | 用户可触达的 `exit(1)` 点清点并改为可捕获错误 | F4 |

### 阶段 1（0.2）——诊断与工程基线

| 项 | 内容 | 价值 |
|---|---|---|
| 1-1 | **源码 span 贯穿**：lexer 产出行列，随 datum 传入展开器与求值器，错误消息带 `file:line:col` | 已有 P1；使 F1 类问题可定位 |
| 1-2 | **栈回溯**：CPS continuation 链天然携带调用链，`npl_report_error_object` 可选打印；REPL `,(backtrace)` | 与 1-1 同批 |
| 1-3 | **分离编译**：`.npl` → 中间产物（SSA/字节码缓存）+ 库清单，`nplc` 增量链接 | workstate 遗留 P2；大程序编译时间 |
| 1-4 | **性能基线**：`bench/` 纳入门禁（防回归阈值），修复已知的读取器 O(n²)（F5）、`string->list` 类热路径 | workstate P1 |
| 1-5 | CI 扩展：ASan/UBSan 档位（`NPL_GC_VERIFY` 常开跑一遍套件）、macOS 门禁上 CI | F10 类问题早发现 |
| 1-6 | 读取器模糊测试（libFuzzer/AFL，输入为字节流，不变量=不崩溃+括号配平） | 安全基线 |

### 阶段 2（0.3）——现代语言特性

| 项 | 路线 | 说明 |
|---|---|---|
| 2-1 | **关键字/可选参数**：SRFI 88（keyword 对象）→ SRFI 89 / 177 | conformance 文档已列；`case-lambda` 已有，属自然补全 |
| 2-2 | **record 进阶**：继承、`#:guard`、opaque、prefab（SRFI 99 / 256） | 与 F3 的边界检查一并做 |
| 2-3 | **Comparators（SRFI 128）**：hashtable/sort/sets 的公共基座 | hashtable 错误消息已引用；先于 SRFI 113/132 |
| 2-4 | **`sort` / SRFI 132、SRFI 13/152 字符串库** | 补齐高频缺口 |
| 2-5 | **库版本选择子**：`(import (scheme base (1 2)))` 风格；与分离编译同批设计 | R7RS 允许；库生态长大前做成本低 |
| 2-6 | **例外安全的标准控制结构**：`unless/when` 已有；考虑 `let-values` 错误路径审计 | 小项 |
| 2-7 | **（决策项）过程式宏**：`syntax-case` 或 `explicit-renaming`/`procedural` 折中；前置条件=编译期求值（`cond-expand` 已有基建）。conformance 文档记录为"用户推迟"，建议保持推迟但写出触发条件（如生态出现刚需） | 决策而非缺漏 |
| 2-8 | **（决策项）对象系统**：维持"records + dispatch"路线（extending.md），观察 SRFI 生态 | 同上 |

### 阶段 3（0.4）——运行时能力与并发

| 项 | 路线 | 说明 |
|---|---|---|
| 3-1 | **线程**：SRFI 18 基础线程 + 互斥/条件变量；SRFI 226 参数对象线程局部化已在 0.1.3 计划。GC 需要并发标记或线程暂停点（stop-the-world 起步即可） | workstate 已排 0.1.3 设计 |
| 3-2 | **弱引用与终结器**：ephemeron 表（补齐 hashtable 的 weak-keys 忽略）、`make-weak-reference` | 非移动式 GC 下实现直接 |
| 3-3 | **socket / 网络库**：`(npl net socket)`，FFI 路径已验证可行（ffi.c） | 生态前提 |
| 3-4 | **进程控制强化**：`run-process` 增加参数数组形式（不经 shell）、环境白名单、超时 | 安全，见阶段 4 |
| 3-5 | **异步/协程上层库**：F1 修复后，用 continuation 实现的 generator/coroutine（SRFI 158/203 风格）可作为库交付，验证修复成效 | 依赖 0-1 |

### 阶段 4（贯穿）——安全特性专项

| 项 | 内容 |
|---|---|
| 4-1 | **FFI 类型安全**：`define-c-function` 的指针类型化为不透明 foreign 对象（解决 F7），指针算子加 null/对齐检查；`--emit=c-stubs` 生成侧加类型注解 |
| 4-2 | **能力式 IO（可选模式）**：`npli --sandbox`：文件/进程/FFI 原语绑定缺省不可见，需显式 `import` 授权；求值器环境已有 mutable/frozen 之分，基建现成 |
| 4-3 | **整数溢出审计**：bignum 路径已有溢出检查（核验过），补 fixnum 上溢静默回绕的负向测试；`/` `quotient` 边界（`most-negative-fixnum`）专项 |
| 4-4 | **GC 加固常态化**：`NPL_GC_VERIFY` 进 CI 矩阵；毒化模式扩展到 remembered set |
| 4-5 | **UB 清零**：F1 的陈旧 jmp_buf、F3 的 OOB、`fseek/ftell` 大文件截断（io.c `long`），配合 UBSan 档位（1-5）逐项清零 |
| 4-6 | **供应链**：vendored QBE 固定 commit + 校验和；`tools/` 脚本 `set -euo pipefail` 审计 |

### 里程碑总览

```
0.1.x  正确性收尾：F1 修复 + 文档 + SRFI train（既有计划）
0.2    诊断基线：span/栈回溯 + 分离编译 + 性能门禁 + sanitizer/模糊测试
0.3    现代语言：关键字参数、record 进阶、SRFI 128/132/13、库版本
0.4    运行时能力：线程、弱引用、socket、generator/coroutine 库
贯穿   安全专项：FFI 类型化、沙箱模式、UB 清零、GC 加固
```

---

## 6. 本次验证记录

本沙箱（Ubuntu, gcc, Unix Makefiles；无 clang/Guile/ninja，ninja 不可用）：

- 重建工具链：`cmake -G "Unix Makefiles" -DCMAKE_C_COMPILER=gcc` + `cmake --build build` → `nplc`、`npli`、QBE、冻结 runtime 全部构建成功（一次 `.size` 汇编错误重跑消失，见 F10-2）。
- continuation 探针：`cc4/cc5/cc6/cc7/cc8` 双路径实测（F1 证据链）；`gen3` 探针确认为测试程序自身缺陷（该程序在任何正确 Scheme 中都会无限输出 `#f`），非实现 bug。
- 上一会话（同一仓库）已跑通：`check_full_compiler.sh`（34 套件 + 77 负向 + SRFI alias parity）、`check_gc.sh`、`check_driver.sh`、`check_install.sh`、`check_stage0*.sh`、`check_bootstrap.sh`、`check_core.sh`、`check_r7rs.sh`、`tools/prop_tests.py`（4/4）——全部 PASS。
- 未能在本沙箱运行：三路 fixture 的 Guile 通道、chibi 套件（1208 pass）、`check_numeric_reference.sh`、bench——均依赖 Guile 3。

## 7. 声明

- 本 review 未对仓库做任何修改；所有探针与构建产物均在 `/tmp` 与 `build/`（gitignored）。
- F1 的根因分析基于源码阅读与 SSA 级核验，修复方案建议在实施前先以差分 fixture 固化现状（先写测试再改行为，符合项目自身流程）。

---

## 附：评审意见（Claude Opus 5.5，2026-11-13）

- **评审对象**：本文档第 1–7 节（以 `c05dd68` 为基准），对照当前 `feature/srfi-i2-i3`（`16de99f`）复核。
- **方法**：逐条读码；用 nplc、npli 实测报告中的探针，并以 Guile 3.0.11、Racket 9.2 作为对照；同时查阅设计文档、workstate 和现有门禁，确认有无已记录的决策或测试。
- **总体结论**：**部分有效，不宜整体接纳**。
  - F3、F5、F6 成立；F4 成立，而且漏掉了一个比报告所举例子更严重的可触达点。
  - 被列为最高优先级的 F1，以及据此推出的 F2、路线图 0-1/0-2/3-5/4-5，前提不成立。所述行为是已决策、已有测试的 REPL/`load` 语义，不是缺陷，也不是 UB。
  - F7 已有防护；F8 未复现。
  - 路线图有几处与仓库现状或用户已作出的决策冲突，详见 A.3。

### A.1 发现逐条复核

| 编号 | 报告结论 | 复核结论 | 依据 |
| --- | --- | --- | --- |
| F1 | 高：npli 跨顶层 form 重入有误且属 UB | **不成立（仅剩文档缺口，低）** | 见 A.2 |
| F2 | escape-only 帧、深度 64 限制了 continuation | **不成立** | `npl_call_cc`/`escape_continuation`（`runtime/trampoline.c:260-322`）是死代码：当前 seed 中引用数为 0，`tools/check_full_compiler.sh:391` 还有一条门禁专门禁止生成它。编译路径和 npli 都走 CPS 堆 continuation。实测两条路径上 1000 层嵌套 `call/cc` 和函数内重入均正确。真正的限制在 FFI：C 函数不得捕获 continuation（`docs/ffi.md:104-106`），这一点已有文档。 |
| F3 | record 访问器无下标边界检查 | **成立（中）** | `runtime/value.c:77-89` 只检查类型。用户可以 `(import (only (npl internal primitive) npl_record_ref ...))`，用越界下标调用：nplc 静默读出垃圾值（得到 `0`），npli 段错误（rc=139）。建议加 `index < size` 检查；修复后成本可忽略。 |
| F4 | 用户可触达的 `exit(1)` | **成立，但例子选错了** | 报告所举 `trampoline.c:290/299` 位于死代码中。实际可触达且更重要的是 `runtime/value.c:376`：带 rest 参数的过程参数不足时，nplc 直接打印 `procedure: invalid rest argument range` 并退出（rc=1），`guard` 捕获不到；而 npli 能捕获，固定参数过程在 nplc 中也能捕获。这同时是一个**双路径分歧**，应优先处理。 |
| F5 | `lex-string` 为 O(n²) | **成立（低）** | `src/compiler/reader/lexer.npl:308-318` 每读一个字符做一次 `string-append`。实测转义密集的字面量从 2 万增加到 8 万个转义时，编译耗时从 2.2 s 涨到 18.3 s。 |
| F6 | `npl_read_file` 不区分短读与读错误 | **成立（低）** | `runtime/io.c:91-93` |
| F7 | FFI 地址存入 fixnum 可能溢出 | **基本不成立** | `runtime/ffi.c:333-336` 打包后会回读比对，不一致时报可捕获错误 "address out of fixnum range"，不会静默溢出；指针值本身另有 `BOX_POINTER` 类型。可以保留为 4-1 的设计整洁项，不是缺陷。 |
| F8 | `pattern-key` 分隔符可能碰撞 | **理论上存在，未复现（极低）** | 用含 `\x1;` 的 `|...|` 标识符做卫生探针，三条路径结果一致。可在 syntax.npl 下次重构时改为结构化键，不必单独排期。 |
| F9 | 无源码位置、无栈回溯 | **成立，但已在计划中** | 即 `.agents/plans/0.1.2-srfi.md` 的 D1（`file: form N`）和 D2（span/栈回溯，排在 0.1.2 之后）。 |
| F10 | 沙箱中测试偶发失败 | **无法复核** | 报告用的是 gcc + Unix Makefiles；仓库门禁用 clang + Ninja，在本机复跑 `check_full_compiler.sh`、fixed point 和 fixture 均通过。可作为对 gcc 构建配置的提示，不构成发现。 |

**事实性错误**（不影响上表结论）：
- "6,753 行 SSA 不动点"指的是冻结的 `bootstrap/bootstrap.ssa`。seed relay 比较的是 `bootstrap/current.ssa`，在 `c05dd68` 时为 263,408 行。
- `src/compiler/**/*.npl` 在 `c05dd68` 为 13,909 行，不是约 14,550 行。
- `check_full_compiler.sh` 的套件数已不止 34 个。
- 运行时、库、stage0 的行数，45 个 fixture，以及 56 档门禁，均核对无误。

### A.2 F1 详细复核

我按报告的 cc5、cc6 探针原样复现，输出与报告完全一致。但结论应该反过来：

1. **对照实现与 npli 一致**：Guile 3.0.11（`--r7rs -s`）和 Racket 9.2（`#lang racket/base` 模块）在 cc5、cc6 上都输出 `11(after 1)` / `42(end 1)`，与 npli 相同。
2. **不跨 form 时两条路径一致**：把同样的逻辑包进一个过程，npli、nplc、Guile 三者输出相同。
3. **这是已作出的设计决策**：`.agents/archive/design/design-evaluator.md` 第 6 节和决策 D3 写明，REPL 中捕获的 continuation 重入时会带上 REPL 循环的剩余部分，并"从端口当前位置继续读取"。这与逐 form 读取求值的 `load` 语义一致。
4. **已有测试固定这一行为**：`src/repl/tests/interop.in` 的第 12–15 行就是"前一个 form 捕获、后一个 form 调用"。`interop.out` 第 6–8 行期望 `101 / 110 / 1`，由 `tools/check_repl.sh:65-68` 检查。报告说"恰好没有覆盖跨顶层 form 重入"，与事实不符。
5. **不涉及 UB**：escape-only 机制不会被调用（见 F2）。cc6 打印 `42` 是 REPL 回显被重入 form 的值，然后从端口继续读下一个 form，属于确定行为。
6. **反证**：`(srfi 158)` 的 `make-coroutine-generator` 跨多个顶层 form 调用，在 npli 下结果正确。

**真正遗留的问题**：
- `npli file.scm` 以 `load` 语义运行脚本，`nplc` 把整个程序视为一条 continuation 链。对"跨顶层 form 重入"的程序，两者输出不同。
- 这个差异只写在归档的设计文档里，`docs/r7rs-conformance.md` 的 Known deviations 和面向用户的 REPL 文档都没有提到。
- R7RS 没有规定顶层 form 之间的 continuation 关系，所以这是一致性/文档缺口（低），不是 conformance 缺陷。

**建议**：
- 在 Known deviations 中加一条，与第 1 条（求值顺序）并列；
- 在 `tests/r7rs/fixtures` 加一个只测"单 form 内重入"的三路 fixture，跨 form 情形继续由 `interop` 覆盖；
- **不要**按报告的"正式修复"把 npli 的顶层 form 串成一条 CPS 链。那会违背 D3 决策，使 npli 与 Guile、Racket 的 REPL/`load` 行为不一致，而且需要定界 continuation 支持。

### A.3 路线图可接纳性

| 条目 | 结论 | 说明 |
| --- | --- | --- |
| 0-1、0-2 | **不接纳，改写** | 改为 A.2 的文档项和 fixture。另外，`npl_call_cc` 这段死代码可以删除，或加注释说明保留原因。 |
| 0-3 | **改写后接纳** | 文档应记录 A.2 的 npli 与 nplc 差异，以及 FFI 中不能捕获 continuation 的约束；"escape 深度 64"不存在，不应写进文档。 |
| 0-4 | 已在执行 | I2/I3 进行中，见 `0.1.2-srfi.md`。 |
| 0-5 | **接纳** | 即 F3。 |
| 0-6 | **接纳** | 先处理 F4 中 rest 参数不足导致的不可捕获退出（双路径分歧），再清点其余 `exit(1)` 点。 |
| 1-1、1-2 | 已计划 | 即 D2，维持 0.1.2 之后的排期。 |
| 1-3 | 已在 Open Items | 不新增。 |
| 1-4 | **修改后接纳** | 修复 F5 可接纳。性能门禁的基准应是相对 Guile/Racket 的比值，而不是本项目自身历史提交的回归阈值；GC 调优按既定安排延后。 |
| 1-5、1-6 | **接纳，建议优先读取器模糊测试** | 本分支 review 刚发现 `#nA` 巨大 rank 导致资源耗尽，读取器是不可信输入的入口，模糊测试的收益最直接。 |
| 2-1、2-2、2-8 | 待决策 | conformance 文档已列为"决策而非遗漏"，这里只是重述，不构成新信息。 |
| 2-3 | 已在计划中 | SRFI 113 依赖 SRFI 128 已写入 0.1.2 计划。 |
| 2-4 | **部分过时** | SRFI 13 已在本分支实现；`sort`/SRFI 132 仍缺，可以接纳。 |
| 2-5 | **不接纳** | 库版本选择子是 R6RS 特性，R7RS-small 的库名里没有版本引用。报告"R7RS 允许"的说法有误。 |
| 2-6 | 不接纳 | 表述过于模糊，无法执行。 |
| 2-7 | **不接纳** | workstate 记录 `syntax-case` 已推迟，且"用户不希望在推迟期间推进"。补写触发条件也算推进，不应列入。 |
| 3-1 | **与现有计划冲突** | SRFI 18/230 和完整 SRFI 226 已排在 0.1.3，报告却放在 0.4，同一节内前后不一。应以现有计划为准。 |
| 3-2、3-3、3-4 | 可作为远期候选 | 没有冲突，可留到 0.2 规划时再评估。 |
| 3-5 | **不成立** | 以"修复 F1"为前提不成立；SRFI 158 协程生成器已经实现，跨 form 可用。 |
| 4-1 | 接纳为设计整洁项 | 参见 F7。 |
| 4-2 | 需要另行设计 | `(npl internal primitive)` 目前可以被任意导入（F3 就借此触发），沙箱的前提是先限制 internal 库的可见性。可变/冻结环境不等于能力隔离。 |
| 4-3、4-4、4-6 | 可接纳 | 低成本；4-3 应注意 O1 内联 fixnum 运算已有溢出回退，负向测试应以此为基础编写。 |
| 4-5 | 部分接纳 | 陈旧 `jmp_buf` 一项不存在（死代码）；记录越界（F3）和 `fseek/ftell` 的 `long` 截断可以接纳。 |

### A.4 建议的后续动作

按优先级排列。以下均为建议，本次未改动任何代码。

1. 修复 F4 中 nplc 下 rest 参数不足导致的不可捕获退出，并加一条差分负向用例：nplc 与 npli 都必须能用 `guard` 捕获。
2. 修复 F3：给 `npl_record_ref/set` 加下标检查，并加一条负向用例（经 internal 原语越界访问）。
3. 文档：在 conformance 的 Known deviations 中补充 A.2 所述的跨 form 重入差异，并补单 form 内重入的三路 fixture。
4. 删除 `npl_call_cc` 死代码，或注释说明保留原因（它已被门禁禁止生成）。
5. 修复 F5（字符累积改为列表再一次性 `list->string`）和 F6。
6. 读取器模糊测试（1-6）。
7. 本文档的路线图不直接并入 `.agents/plans/`。可接纳条目（0-5、0-6、F5/F6、1-5/1-6、`sort`）应以增量形式补进 0.1.2/0.1.3 计划；其余维持现有排期或决策。

**复核证据**：探针脚本 `cc5`、`cc6`、`cc5-body`、`deep`、`rec`、`ar`、`ar2`、`gen`、`hyg`、`big*` 位于 `/tmp/npl-review/rm/`（仓库外）。所有结论均来自上表所列命令的实际输出。
