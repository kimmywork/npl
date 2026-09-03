# R7RS-small 标准库完整实现与 Guile 验证计划

状态：**待用户审核；未获批准前不得执行实现增量**

## 0. 目标与完成定义

### 0.1 目标

完成 `docs/reference/r7rs.html` 中 R7RS-small 标准库的实现，使 NPL 对标准库的可观察行为与 R7RS 规定一致，并在以下三种执行方式上验证同一份语义契约：

1. NPL 编译后的 native executable；
2. NPL 持久 REPL；
3. Guile 的 R7RS 执行环境，作为外部参考实现和对照基线。

这里的“严格一致”指：对 R7RS 定义的输入、结果、异常类别、可变性、端口行为、动态状态和边界条件，native 与 REPL 不得有意采用不同语义；Guile 对照用于发现 NPL 与标准行为的差异。实现内部可以复用已有代码，但不能以“执行路径不同”为理由接受可观察行为差异。

### 0.2 完成定义

最终计划只有在以下条件全部满足时才算完成：

- R7RS-small 标准库的全部规定库名、导出名和语法导出已建立 binding inventory；
- 每个标准导出项都有以下三种状态之一，并有证据：
  - `implemented`: 语义、边界、错误行为和性能基线均已验证；
  - `not applicable`: 由 R7RS 明确允许实现依赖且已记录理由；
  - `blocked`: 尚未完成，不能宣称总体完成；
- 不得把“库名可以 import”或“基本例子通过”当作语义完成；
- 同一份 conformance fixture 在 native、持久 REPL 和 Guile 上执行；
- native 与 REPL 的标准输出、标准错误、退出状态、返回值和可观察副作用一致；
- 与 Guile 的差异逐项归类为：NPL bug、fixture 错误、Guile 非 R7RS 扩展、实现允许差异或尚未实现；未归类的差异不得关闭；
- 性能 benchmark 在固定环境、固定输入和固定重复次数下完成，并保留原始数据；
- 全部项目 gate 通过；
- `bootstrap/compiler/` 与 `bootstrap/bootstrap.ssa` 未修改；
- 任何未完成项都保留在最终 inventory 中，不得用文档措辞掩盖。

## 1. 不可变约束

以下约束适用于每一个增量，不得因实现困难而临时改变：

1. **先计划、后实现。** 本文未获批准前只允许审阅和修订计划，不允许开始代码实现。
2. **严格按编号执行。** 每完成一个增量就停止，记录证据，确认该增量完成后再进入下一个增量。
3. **不得扩大范围。** 不引入 JIT、优化器重启、control IR、第二套宏/reader/import 语义、包管理、版本求解、动态库加载或与标准库无关的重构。
4. **不得以失败路线残留充当功能。** 探索性 primitive、临时 bridge、调试 API 在最终路径没有证明必要时必须删除。
5. **永久 bootstrap 冻结。** 不修改 `bootstrap/compiler/` 和 `bootstrap/bootstrap.ssa`。
6. **current SSA 只在必要时更新。** 只有 `src/compiler/` 或 runtime C 的实际变化才执行 fixed-point relay；library-only 变化不更新 SSA。
7. **NPL-first。** 标准库策略、算法、参数检查和错误分类放在 NPL；C 只承担 runtime 机制、OS/libc wrapper 和必要 FFI。
8. **一个行为一份契约。** 不为 native 和 REPL 编写两套标准语义。两者必须消费相同的行为 fixtures 和相同的 expected-result 规范。
9. **错误也属于语义。** 不能只比较成功结果；类型错误、范围错误、读错误、文件错误、异常、EOF、退出状态和资源关闭行为都必须测试。
10. **不宣称超出证据的 conformance。** 有限 fixture 只能证明有限行为。

## 2. 执行规则与增量模板

每个增量都必须先填入并审核以下信息，随后才能修改代码：

- 增量编号和唯一目标；
- 包含的标准库、导出项和语义章节；
- 依赖的已完成增量；
- 允许修改的文件类别；
- 明确不包含的内容；
- native、REPL、Guile 三方测试列表；
- benchmark 列表和计时口径；
- 完成条件；
- 失败时的停止点。

每个增量的执行顺序固定为：

1. 读取 R7RS 对应章节，更新 inventory 和语义 checklist；
2. 检查现有实现和已有测试，确认复用边界；
3. 编写或补充共同 fixture、expected output 和 expected error 分类；
4. 先实现最小必要代码，不添加未列入增量的 API；
5. 编译 native 并运行 focused suite；
6. 在持久 REPL 中运行同一 fixture 的等价 transcript；
7. 在 Guile R7RS 环境中运行同一语义 fixture；
8. 做三方差异归类；
9. 只运行当前增量直接相关的最小 focused correctness checks；
10. 只有当当前增量改变了性能相关的表示、算法、runtime 路径，或完成一个语义族时，才运行对应 benchmark；
11. 只有 compiler/runtime 改动或一个语义族完成时，才运行相应的完整 repository gate；
12. 审查完整 diff、永久 bootstrap 状态、inventory、worklog 和 workstate；
13. 只有全部适用检查通过才提交该增量。

任何一步失败，都只能修复当前增量；不得顺手进入下一个库或重构相邻模块。

## 3. 依赖关系与严格执行顺序

上一版计划将完整 `(scheme base)` 放在 number tower 之前，这个顺序不成立，因为 `(scheme base)` 自身导出数值、端口、字符、字符串、bytevector、控制和转换功能。下面的依赖图是唯一执行顺序；第 5 节的 C 编号用于标识工作范围，不表示可以按编号跳过依赖。

### 3.1 语义依赖图

```text
A0 基线/文档审计/Guile 环境
  |
  v
A1 binding inventory + A2 shared fixture runner
  |
  +--> B1 值表示、类型、错误、可变性、对象身份
  |      |
  |      +--> B2 已有控制/异常/动态状态/multiple-values 基线（已实现，不重做）
  |      |
  |      +--> B3 textual/binary ports、EOF、close/flush、资源状态
  |      |      |
  |      |      +--> B4 reader/writer datum 语义
  |      |
  |      +--> B5 Unicode char/string/UTF-8 语义
  |
  +--> C4 number tower、complex、inexact、数值 parse/print
  |      |
  |      +----------------------------------+
  |                                         |
  |                    +--> C1 (scheme base) 完整导出闭包
  |                                   |
  |                                   +--> C2a (scheme cxr)
  |                                   +--> C2b (scheme case-lambda)
  |                                   +--> C3 (scheme char)
  |                                   +--> C5 (scheme read)
  |                                   +--> C5 (scheme write)
  |                                   +--> C6 (scheme file)
  |                                   +--> C7 (scheme process-context)
  |                                   +--> C8 (scheme time)
  |                                   +--> C9 (scheme lazy)
  |                                   |
  |                                   +--> C10 (scheme eval)
  |                                                  |
  |                                                  +--> C10 (scheme repl)
  |                                                  |       |
  |                                                  +-------+--> C10 (scheme load)
  |                                                                  |
  |                                                                  v
  |                                                             C11 (scheme r5rs)
```

### 3.2 每个节点的前置条件

| 节点 | 前置条件 | 直接产出 |
|---|---|---|
| A0 | 无 | 基线、R7RS章节索引、Guile版本和机器记录 |
| A1 | A0 | 逐导出 binding inventory |
| A2 | A0 | 共用 fixture metadata、三方 runner、差异分类器 |
| B1 | A1 | 统一值、类型、错误、mutation 和 identity 契约 |
| B2 | B1 | 已有控制、异常、动态状态和 multiple-values 契约（基线，不新增实现） |
| B3 | B1 | text/binary port 和资源生命周期契约 |
| B5 | B1 | Unicode char/string/UTF-8 契约 |
| C4 | B1 | 整数、bignum、rational、real、complex、inexact 和数值 I/O |
| B4 | B3、B5、C4 | 完整 datum read/write 的输入输出语义 |
| C1 | B1、B2、B3、B4、B5、C4 | `(scheme base)` 全部导出 |
| C2a | C1 | `(scheme cxr)` |
| C2b | C1 | `(scheme case-lambda)` |
| C3 | C1、B5 | `(scheme char)` 全部导出 |
| C5 | C1、B3、B4 | `(scheme read)` 与 `(scheme write)` |
| C6 | C1、B3、文件系统机制 | `(scheme file)` |
| C7 | C1、进程环境机制 | `(scheme process-context)` |
| C8 | C1、时钟机制 | `(scheme time)` |
| C9 | C1、B2 | `(scheme lazy)` |
| C10-eval | C1、B2、库解析/宏/reader 契约 | `(scheme eval)` |
| C10-repl | C10-eval、C5、C7 | `(scheme repl)` |
| C10-load | C10-eval、C5、C6、C10-repl 的 interaction-environment 契约 | `(scheme load)` |
| C11 | C1、C2、C3、C4、C5、C6、C7、C8、C9、C10 | `(scheme r5rs)` 兼容导出层 |

### 3.3 依赖规则

- 这是**语义依赖**，不是要求标准库源码互相 import；为避免公共库循环，库实现可以使用已经存在的私有实现边界，但最终导出行为必须遵循此图。
- `C4` 必须在 `C1` 完成前关闭；否则 base 的数值导出会被错误地标记为完成。
- `B4` 必须在 `C4` 后关闭；reader/writer 必须能处理最终数值表示，而不是只处理 fixnum。
- `C10-load` 必须在 `C10-eval` 和 interaction-environment 契约稳定后关闭；不能先用临时 environment identity 协议补齐。
- `C11` 永远最后执行；不能用尚未完成的底层语义生成看似完整的 R5RS export。
- 任何节点发现需要新的前置节点时，当前节点停止，计划回到审核状态；不得现场扩展依赖图。

### 3.4 阶段 A：基线冻结、审计与 harness

### A1. 审计当前工作区

**结果：** 得到与实际代码一致的基线报告。

检查：

- 当前 HEAD、工作区状态和未提交改动；
- `880d74d` 的完整 diff；
- 当前所有 `lib/scheme/*.sld`、`lib/npl/*.sld`、primitive registry、runtime ABI；
- native 与 REPL 当前标准库导出；
- `bootstrap/current.ssa` 行数和 SHA-256；
- 现有 focused tests、REPL transcripts 和 tools gate。

特别检查并单独列出此前 load 探索留下的非必要 evaluator primitive，包括但不限于：

- `npl_lex_class_w`；
- `npl_lex_scan_sym_end`；
- `npl_lex_number`；
- 仅为失败路线加入的 pair/string/file primitive。

这些内容在没有最终路径必要性证据前不得视为标准库实现的一部分。

**完成条件：** 审计报告与代码事实一致；任何文档中“已实现”“限制”“所需 primitive”的描述都能在 diff 和测试中找到证据。

### A2. 建立完整 R7RS binding inventory

**结果：** `docs/r7rs-binding-inventory.md`，一项一行。

来源：

- `docs/reference/r7rs.html` 的标准库总览；
- 各标准库章节；
- 每个 procedure、syntax、constant、record/error object 的定义章节。

必须覆盖的标准库：

- `(scheme base)`；
- `(scheme case-lambda)`；
- `(scheme char)`；
- `(scheme complex)`；
- `(scheme cxr)`；
- `(scheme eval)`；
- `(scheme file)`；
- `(scheme inexact)`；
- `(scheme lazy)`；
- `(scheme load)`；
- `(scheme process-context)`；
- `(scheme read)`；
- `(scheme repl)`；
- `(scheme time)`；
- `(scheme write)`；
- `(scheme r5rs)`。

每项至少记录：库名、标识符、类型（procedure/syntax/variable）、R7RS 章节、当前状态、现有实现位置、测试位置、native 状态、REPL 状态、Guile 状态、已知差异和依赖。

**完成条件：** 标准库总览中的每个导出名均有记录；不存在只记录库名而没有逐项行为的“假完成”。

### A3. 建立统一 conformance harness

**结果：** 三方可重复运行的验证工具和 fixture 目录。

建议目录：

```text
tests/r7rs/
  inventory/
  common/
  native/
  repl/
  guile/
  expected/
  differential/
  benchmarks/
  reports/
```

每个 fixture 采用以下规则：

- source 尽量只使用目标增量已经允许的 R7RS 导出；
- 不依赖 Guile 私有模块；
- 不依赖 NPL 私有 primitive；
- 输出采用可比较的标准化格式；
- 对非确定值（时间、地址、随机性）使用关系断言或范围断言；
- 对错误比较错误类别和恢复行为，不硬编码不属于 R7RS 的实现错误文本；
- 每个 fixture 标记期望的退出状态、stdout、stderr、文件副作用和是否应继续执行。

统一执行矩阵：

| Fixture | NPL native | NPL persistent REPL | Guile R7RS |
|---|---:|---:|---:|
| 成功值和输出 | 必须 | 必须 | 必须 |
| 错误类别和退出/恢复 | 必须 | 必须 | 必须 |
| 文件/端口副作用 | 必须 | 必须 | 必须 |
| 动态状态 | 必须 | 必须 | 必须 |
| 性能样本 | 必须 | 必须 | 必须 |

**完成条件：** 同一 fixture 的三方 runner 已能独立运行；差异报告能指出 fixture、输入、三方结果和分类。

### A4. Guile 基准环境确认

记录：

- `guile --version`；
- Guile 是否支持并如何启用 R7RS；
- 操作系统、CPU、内存、编译器、QBE 版本；
- NPL 编译参数；
- Guile 优化/编译参数；
- locale 和 timezone；
- benchmark 使用的文件系统和临时目录。

Guile 只能作为 R7RS 对照。Guile 扩展行为不能反过来改变 NPL 的标准契约。若 Guile 不支持某个标准边界，必须记录为 Guile oracle limitation，并使用 R7RS 文档和独立关系断言补足。

## 4. 阶段 B：确认已有共同语义基础，不重复实现

B 阶段不是重新实现所有基础语义。现有 compiler/native 和 evaluator/REPL 已经实现了其中的大部分能力；本阶段只建立依赖事实和最小缺口记录。只有某个后续标准库 fixture 暴露出具体 R7RS 差异，才在该标准库增量内修复该差异，不单独启动一轮重复实现。

### B1. 值表示、类型谓词和可变性

状态：现有能力作为基线复用；只做 inventory 对照。

覆盖：

- boolean、char、symbol、string、pair、vector、bytevector、port、procedure、number；
- `eq?`、`eqv?`、`equal?` 的 R7RS 可观察规则；
- mutable/immutable 对象；
- literal datum 的不可变性；
- `set-car!`、`set-cdr!`、vector/string/bytevector mutation 的边界；
- `procedure?` 和跨调用 procedure 行为。

验证重点：对象身份、共享结构、循环结构、越界、错误类型、GC 后存活、native/REPL 一致性。

### B2. 已有控制、异常、动态状态和 multiple-values 基线

状态：**已实现，不得重做**。

现有实现已覆盖并有既有测试证据：

- `call/cc` / `call-with-current-continuation`；
- `call-with-values`、`values`、`define-values`、`let-values`、`let*-values`；
- `dynamic-wind`；
- `with-exception-handler`、`raise`、`raise-continuable`、`guard`；
- `make-parameter`、`parameterize`；
- promise 相关动态上下文。

执行规则：

- 不新增 B2 实现阶段；
- 不为标准库计划重复建立一套控制/异常语义；
- 后续库只在实际使用这些能力时运行最小相关回归；
- 若 fixture 发现具体差异，只修复该差异所属的标准库增量；
- evaluator multiple-values 跨 public eval 边界等已知缺口，留在 C10 的对应 crossing work 中，不在 B2 提前扩张。

B2 的直接产出只是“已有基线可作为依赖”，不是新的代码产出。

### B3. 端口和资源基础

状态：现有能力作为基线复用；只补后续标准库实际证明缺失的语义。

覆盖：

- textual/binary port 类型；
- open/closed 状态；
- current input/output/error ports；
- EOF object；
- close、flush、call-with-port；
- 读写失败和资源释放。

必须验证正常结束、异常结束、错误结束三种路径都会执行规定的关闭行为。

### B4. reader/writer datum 语义

状态：现有 reader/writer 能力作为基线复用；只补最终 number/Unicode/port 语义所证明的具体缺口。

覆盖：

- 数字、字符、字符串、符号、列表、点对、向量、bytevector；
- quote/quasiquote/unquote/unquote-splicing；
- datum labels 和共享/循环 datum；
- escaped strings、大小写、非法输入、read error；
- `write`、`write-shared`、`write-simple`、`display` 的可观察规则。

这一阶段不扩展非 R7RS 语法；Unicode reader 行为列入 B5。

### B5. Unicode 基础

状态：当前仅有既定 ASCII 基线；Unicode 扩展是实际的新实现增量。

覆盖：

- 字符和字符串的 Unicode scalar value 表示；
- `integer->char` / `char->integer` 的合法范围；
- Unicode 字符分类、大小写、foldcase；
- 字符串长度、索引、截取、比较、映射和转换的字符语义；
- UTF-8 编码/解码、非法序列和 replacement/error 行为；
- locale 不应改变 R7RS 定义的 Unicode case-fold 结果。

**阶段完成条件：** B1、B2、B3、B4 的现有基线已在 inventory 中标明并被后续增量复用；B5 的 Unicode 新增行为有明确范围。不得因为 B 阶段存在就重复执行已完成的控制、异常或 multiple-values 实现。

## 5. 阶段 C：标准库按依赖顺序完成

每个 C 增量均必须单独通过第 2 节的完整执行顺序。

### C1. `(scheme base)` 完整导出

逐项完成并验证：

- 算术和数值谓词：`+ - * / = < <= > >= abs ceiling floor round truncate`；
- 整数除法族：`div div0 mod mod0`, `div-and-mod`, `div0-and-mod0` 及 R7RS 对应名称；
- 数值转换和精度：`exact inexact exact? inexact? exact-integer? finite? infinite? nan?`；
- `number? complex? real? rational? integer?`；
- `expt`, `square`, `gcd`, `lcm`, `numerator`, `denominator`, `rationalize`, `exact-integer-sqrt`；
- boolean、pair/list、association/member、mutation；
- symbol 和 symbol comparison；
- 完整 string API；
- 完整 vector API；
- 完整 bytevector API；
- char 基础 API；
- ports、string ports、bytevector ports；
- `map`、`for-each`、`string-map`、`vector-map`、`string-for-each`、`vector-for-each`；
- `include`、`include-ci`、`cond-expand`；
- derived syntax 和控制/异常/参数/promise 导出；
- `define-record-type`、`syntax-rules`、`syntax-error`；
- `features`、`error`、error object API。

该增量只有在 base 的 inventory 全部关闭后才算完成；不能以当前已覆盖的子集为完成。

### C2a/C2b. `(scheme cxr)` 和 `(scheme case-lambda)`

- 完成全部 24 个三层/四层 `car`/`cdr` 组合；
- 验证 improper list、非 pair、错误位置；
- 完成 `case-lambda` 的所有 clause 匹配、固定/可变参数、无匹配错误和 tail behavior；
- 验证导出隔离：未 import 时不可见。

### C3. `(scheme char)`

完成：

- alphabetic/numeric/whitespace/lower/upper predicates；
- `digit-value`；
- char case conversion and foldcase；
- case-sensitive and case-insensitive character/string comparisons；
- Unicode boundary and non-ASCII fixtures；
- ASCII 与非 ASCII 的结果不得分叉。

### C4. `(scheme complex)`、`(scheme inexact)` 与 number tower

先冻结表示和语义设计，再编码。设计必须覆盖：

- fixnum 到 bignum promotion；
- 任意精度整数算术；
- exact rational 约分、符号、零分母错误；
- exact/inexact conversion；
- real/complex promotion；
- rectangular/polar construction；
- `real-part`、`imag-part`、`magnitude`、`angle`；
- NaN、positive/negative infinity、signed zero（若平台支持）；
- `sin cos tan asin acos atan exp log sqrt`；
- mixed exact/inexact operation；
- numeric equality、ordering、hash/identity behavior where specified；
- decimal/exponent/radix parsing and printing；
- overflow、underflow、domain error 和 non-finite result；
- `number->string` / `string->number` 的 radix、精度和非法输入。

每种数值表示必须有 property tests、边界 tests、随机/生成式 tests，并有 native/evaluator differential fixture。benchmark 至少包括大整数、长分数、复数算术、三角/对数、parse/print。

### C5. `(scheme read)`、`(scheme write)`

- 完成 `read` 的 port/default-port、EOF、read error 和 datum labels；
- 完成 `write`、`write-shared`、`write-simple`、`display`；
- 共享/循环结构输出必须有关系型验证；
- 字符、字符串、Unicode、数值 tower 的打印/读取必须互相验证；
- native stdout 与 REPL transcript 输出标准化后必须一致。

### C6. `(scheme file)`

完成：

- `open-input-file`、`open-output-file`；
- binary input/output file；
- `call-with-input-file`、`call-with-output-file`；
- `with-input-from-file`、`with-output-to-file`；
- `file-exists?`、`delete-file`；
- open mode、truncate/overwrite、close、flush、missing path、permission failure；
- exception/error recovery 后文件和 port 状态；
- 路径含空格、Unicode 路径、相对/绝对路径。

### C7. `(scheme process-context)`

完成：

- `command-line` 的列表内容和顺序；
- `get-environment-variable`；
- `get-environment-variables`；
- `exit`；
- `emergency-exit`；
- 正常退出、非零退出、异常退出、REPL 中退出行为；
- 环境变量不存在、重复/排序规则和平台差异记录。

不得把 NPL 私有 child-process API 当作 R7RS export。

### C8. `(scheme time)`

完成：

- `current-second`；
- `current-jiffy`；
- `jiffies-per-second`；
- 单调性、整数性、分辨率、溢出和 epoch 关系；
- native 与 REPL 的类型和关系行为；
- 与 Guile 比较关系，不比较实现特有 epoch 数值；
- benchmark 计时使用单独的 monotonic host clock，不把被测 API 的结果当 benchmark clock。

### C9. `(scheme lazy)`

完成并重验：

- `delay`、`delay-force`、`force`、`make-promise`、`promise?`；
- memoization；
- recursive forcing；
- dynamic handler/parameter/wind context；
- long `delay-force` chains；
- multiple values through promises；
- error during first force and subsequent force behavior。

### C10. `(scheme eval)`、`(scheme load)`、`(scheme repl)`

这三个库必须按 R7RS 对外行为完成，不保留当前单参数/子集声明。

`(scheme eval)`：

- `environment` 的 import set 解析；
- `only`、`except`、`prefix`、`rename` 组合；
- value binding、syntax binding 和 core syntax 的准确可见性；
- `eval` 的 expression/definition 行为；
- environment-specifier 的身份、隔离、可变性和生命周期；
- procedure、multiple values、异常、continuation 跨边界；
- native 与 REPL 中同样的 environment 行为；
- 失败后 environment/session 可继续使用。

`(scheme load)`：

- `(load filename)`；
- `(load filename environment-specifier)`；
- 逐 datum 顺序执行；
- EOF、读错误、求值错误、文件错误；
- 规定的关闭和错误传播；
- 两个参数形式在 native 和 REPL 中使用同一 environment identity 规则。

`(scheme repl)`：

- `interaction-environment`；
- 交互绑定的初始状态；
- 与 `eval`、`load` 的一致性；
- 持久输入、错误恢复、定义和宏的持续可见性；
- 不允许 REPL 私有行为改变标准 procedure 的结果。

### C11. `(scheme r5rs)`

只实现 R5RS 兼容导出层：

- 逐项从已经完成的 R7RS 语义导出；
- `inexact->exact` / `exact->inexact` 等名称映射必须有明确测试；
- 不加入 `transcript-on` / `transcript-off`；
- 任一底层语义未完成时，不得把对应 R5RS 名称标为完成；
- 验证 R5RS import 与各独立 `(scheme ...)` 库的 binding 冲突和覆盖规则。

## 6. Guile 正确性验证协议

### 6.1 Fixture 规范

每个标准行为 fixture 生成三份执行输入：

- `program.npl`：native 编译输入；
- `transcript.in`：持久 REPL 输入；
- `program.scm`：Guile R7RS 输入。

三份输入表达同一行为，不要求文本完全相同。每份都必须由同一 fixture metadata 指定预期结果。

### 6.2 结果比较

比较以下字段：

- normalized stdout；
- normalized stderr category；
- exit status；
- returned datum/result count；
- file tree and file bytes；
- port closed/open state；
- mutation/shared-structure observations；
- dynamic event trace；
- elapsed time and peak RSS for benchmark only。

禁止只比较“程序没有崩溃”。

### 6.3 差异处理

每个差异必须有一个记录：

```text
fixture:
api:
input:
npl-native:
npl-repl:
guile:
classification:
source-of-truth:
resolution:
regression-test:
```

`classification` 只能使用：

- `npl-bug`；
- `guile-extension`；
- `implementation-allowed`；
- `fixture-bug`；
- `unimplemented`；
- `oracle-unavailable`。

`unimplemented` 和 `oracle-unavailable` 不能被算作通过。

## 7. 性能 Benchmark 计划

### 7.1 公平性规则

- 固定机器、OS、CPU governor、locale、timezone 和环境变量；
- 记录 Guile/NPL/QBE/compiler 版本；
- 关闭输出，或将输出写入相同的 sink；
- 使用相同输入数据和算法；
- 分离编译时间、启动时间和稳态执行时间；
- 每个样本至少预热 3 次，正式运行至少 10 次；
- 报告 median、min/max、p95，并保留原始样本；
- 对 REPL 单独测量启动成本和第 N 次输入的稳态成本；
- 不把 NPL 编译器耗时和程序执行耗时混在一起；
- 不把 Guile JIT/bytecode/native compilation 与 NPL native executable 的模式混在一个数字中，必须分组报告。

### 7.2 Benchmark 组

**语言与调用：**

- 递归/迭代斐波那契；
- 十亿级或等效长 tail loop；
- closure、higher-order apply、map/for-each；
- exception、continuation、dynamic-wind、parameterize；
- promise force 和长 `delay-force` chain。

**数据结构：**

- list build/traverse/append/reverse；
- association/member；
- vector build/read/write/map；
- bytevector build/read/write/copy；
- string build/index/copy/map/compare；
- shared/cyclic datum write/read。

**数值：**

- fixnum arithmetic；
- 4096/65536-bit bignum arithmetic；
- rational normalization and arithmetic；
- complex arithmetic；
- parse/print number；
- transcendental functions。

**I/O 与库：**

- read/write datum throughput；
- textual and binary port throughput；
- file read/write；
- `load` of N forms；
- import/library resolution and cache behavior；
- `eval` of repeated expressions；
- REPL repeated input latency。

### 7.3 Benchmark 完成条件

benchmark 不用于把“更快”当作“更正确”。每个 benchmark 只有在对应 correctness fixture 三方通过后才可纳入结果。报告至少包括：

- workload description；
- input size；
- implementation mode；
- compile/startup/steady-state timing；
- memory peak；
- repetitions and raw samples；
- Guile baseline；
- NPL native ratio；
- NPL REPL ratio；
- variance and known causes；
- 不可比较项。

## 8. 每个增量的固定验收 Gate

### 8.1 每个增量的最小 focused gate

- 当前增量涉及的 positive fixtures；
- 当前增量涉及的 arity/type/range/error fixtures；
- 至少一个 native executable 运行；
- 同一行为的 persistent REPL transcript；
- 同一行为的 Guile R7RS comparison；
- 当前差异已分类。

不相关的库、benchmark 和全量套件不在 focused gate 中重复运行。

### 8.2 语义族完成时的 gate

当一个语义族（例如 number tower、Unicode、ports 或 eval/load/repl）全部完成时，额外运行：

- 该语义族的完整 native suite；
- 该语义族的完整 persistent REPL suite；
- 该语义族的完整 Guile differential suite；
- 该语义族规定的 benchmark；
- 本节 8.3 的 repository gate。

### 8.3 repository gate

对于 library/evaluator-only 改动：

```bash
./tools/check_repl.sh
./tools/run_tests.sh --skip-current-fixed-point
git diff --check
```

对于 compiler/runtime source 改动：

```bash
./tools/update_current_ssa.sh
./tools/run_tests.sh --skip-current-fixed-point
./tools/check_repl.sh
git diff --check
```

并且必须额外确认：

```bash
git diff -- bootstrap/compiler bootstrap/bootstrap.ssa
```

为空；current SSA 的 gen1/gen2/gen3 byte-identical；记录行数和 SHA-256。

### 8.4 提交 gate

提交前必须：

- 查看完整 `git diff`，不是只看 stat；
- 删除未证明必要的探索性代码；
- 检查标准库 export inventory 与代码一致；
- 检查 native/REPL/Guile 报告没有未分类差异；
- 更新 `.agents/worklog/YYYY-MM-DD.md` 和 `.agents/workstate.md`；
- 在提交说明中写明该增量，不得把多个标准库阶段混成一个提交；
- 报告未完成项和性能限制。

## 9. 停止条件

以下任一情况出现，都必须停止当前增量并报告，不得自行扩张：

- 需要引入本文未列出的新架构；
- 需要修改永久 bootstrap；
- 需要增加与当前增量无关的 primitive；
- native 与 REPL 出现语义差异且无法在当前增量内修复；
- Guile 差异无法分类；
- fixture 通过但 inventory 仍有未覆盖导出；
- benchmark 结果不可公平比较；
- fixed-point relay 失败；
- 发现现有已提交功能与标准契约冲突；
- 需要改变已审核的表示、环境身份或错误协议。

停止后只允许做三件事：报告证据、更新风险/决策记录、请求重新审核范围。不得通过临时 fallback、额外私有导出或文档降级绕过停止条件。

## 10. 最终交付物

计划完成后必须存在：

```text
docs/r7rs-binding-inventory.md
docs/r7rs-conformance-report.md
docs/r7rs-guile-differential-report.md
docs/r7rs-benchmark-report.md
```

以及：

- 每个标准库对应的 native focused tests；
- 每个标准库对应的 persistent REPL transcript；
- Guile 对照 runner 和版本记录；
- benchmark runner、原始数据和汇总数据；
- 最终 current SSA relay 证据（仅在实际需要更新时）；
- 最终 worklog/workstate；
- 明确的 completed/blocked inventory。

在所有交付物完成前，项目状态只能写为“R7RS-small conformance in progress”，不能写为“R7RS-small complete”。
