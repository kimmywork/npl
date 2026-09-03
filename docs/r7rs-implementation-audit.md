# R7RS-small 当前实现全量审计

状态：**基线审计完成；Guile harness 和新实现均未开始**

审计日期：2026-09-03

## 1. 审计目的

本报告回答一个问题：在开始任何新的 R7RS-small 标准库实现前，仓库中哪些能力已经存在、哪些只是部分存在、哪些缺失、哪些已有代码但没有足够的标准行为证据。

本报告只记录当前事实，不把库名可解析、导出名存在或有限测试通过等同于完整 R7RS conformance。

## 2. 使用的证据

### 2.1 规范来源

- `docs/reference/r7rs.html`
- 标准库总览中的每个 `(scheme ...)` export list
- 对应 procedure/syntax 章节

### 2.2 实现来源

- `lib/scheme/*.sld`
- `lib/npl/*.sld`
- `lib/npl/evaluator-core.scm`
- `src/compiler/reader/`
- `src/compiler/frontend/`
- `runtime/`
- `tools/check_full_compiler.sh`
- `tools/check_repl.sh`
- `src/compiler/tests/`
- `src/repl/tests/`

### 2.3 当前工具和基线

```text
guile 3.0.11
bootstrap/current.ssa: 348,879 lines
bootstrap/current.ssa SHA-256:
ed6f3a59116ac86cff1dc24bbc07562470ab140b2dc5cb058e054cf66f93509e
```

永久 bootstrap 文件未在本次审计中修改。

## 3. 状态定义

| 状态 | 含义 |
|---|---|
| `existing-tested` | 已有实现，并且当前测试对该行为有直接证据；不重做 |
| `existing-partial` | 已有实现或导出，但有已知边界、执行面或 R7RS 语义缺口 |
| `surface-present` | 库/导出/源码实现存在，但尚无足够的逐项标准行为证据 |
| `missing` | 标准库或标准导出当前不存在 |
| `wrong-surface` | 有实现但导出位置、可见性或公共 API 与 R7RS 不一致 |
| `deferred` | 已明确延期，不能被当作完成 |

## 4. 标准库总表

标准库导出数量来自 R7RS reference library summary；当前导出数量来自 `lib/scheme/*.sld` 的实际 export form。

| 标准库 | R7RS 导出数 | 当前导出数 | 当前状态 | 主要证据/缺口 |
|---|---:|---:|---|---|
| `(scheme base)` | 238 | 102 | `existing-partial` / `wrong-surface` | 大量导出缺失；lazy 导出错误地出现在 base；现有语言和 first-wave 测试只覆盖子集 |
| `(scheme case-lambda)` | 1 | 1 | `surface-present` | `lib/scheme/case-lambda.sld`；需要逐项 clause/error/tail conformance |
| `(scheme char)` | 22 | 19 | `existing-partial` / `wrong-surface` | 缺少 8 个 string-ci/string case 导出；5 个 case-sensitive char 导出位于 char 而不是 base |
| `(scheme complex)` | 6 | 0 | `missing` | 库文件不存在 |
| `(scheme cxr)` | 24 | 24 | `surface-present` | 24 个组合器源码存在；需要标准错误边界确认 |
| `(scheme eval)` | 2 | 2 | `existing-partial` | `environment`/`eval` 存在；procedure crossing、multiple-values crossing、syntax visibility 有已知缺口 |
| `(scheme file)` | 10 | 6 | `existing-partial` | 缺少四个 call/with/with-file wrapper |
| `(scheme inexact)` | 12 | 0 | `missing` | 库文件不存在 |
| `(scheme lazy)` | 5 | 5 | `existing-tested` | promise 行为已有 evaluator/native/REPL 测试；仍需逐项公共库 conformance |
| `(scheme load)` | 1 | 1 | `existing-partial` / `deferred` | 单参数顺序 load 可用；environment-specifier 形式未完成 |
| `(scheme process-context)` | 5 | 5 | `surface-present` | 现有 process 测试；需要逐项标准进程环境和退出语义 |
| `(scheme read)` | 1 | 1 | `surface-present` | `read` 已导出；reader/error/port 组合需要标准库级覆盖 |
| `(scheme repl)` | 1 | 1 | `existing-partial` | interaction environment 已有；与完整 eval/load identity 语义仍需统一 |
| `(scheme time)` | 3 | 0 | `missing` | 库文件不存在 |
| `(scheme write)` | 4 | 4 | `surface-present` | 四个名字已导出；shared/simple/datum/Unicode 行为未逐项关闭 |
| `(scheme r5rs)` | 222 | 0 | `missing` | 兼容导出层不存在 |

R7RS 的 `(scheme r5rs)` summary 在 HTML 中包含示例文本，实际 export inventory 以该节的完整 identifier 列表为准；本审计不把示例 datum 计作导出。

## 5. 已有能力：不能重复实现的部分

### 5.1 控制、异常、动态状态、multiple values

状态：`existing-tested` 作为当前项目基线；**不得启动新的 B2 实现阶段**。

现有实现和直接测试包括：

- `call/cc` / `call-with-current-continuation`；
- `values` / `call-with-values`；
- `define-values` / `let-values` / `let*-values`；
- `dynamic-wind`；
- `with-exception-handler`；
- `raise` / `raise-continuable`；
- `guard`；
- `make-parameter` / `parameterize`；
- continuation wind/handler/parameter state restoration；
- promises 的动态 handler/parameter/wind 上下文；
- native tests and evaluator/REPL tests。

现有直接证据：

```text
tools/check_full_compiler.sh:
  multiple-values 13
  continuations 7
  dynamic-wind 5
  exceptions 6
  guard 5
  parameters 10
  promises 10

tools/check_repl.sh:
  evaluator-core 42
```

注意：`existing-tested` 表示这部分已有实现和项目测试基线，不表示所有 future public eval crossing 都完整。public eval 的 multiple-values 转换和 evaluator continuation 的边界问题属于 eval crossing 缺口，不能反过来触发控制系统重做。

### 5.2 reader、datum、宏和 library resolver

状态：`existing-partial`；**不重新实现第二套 reader/macro/import 语义**。

已有能力：

- ASCII repertoire 的 reader/datum；
- dotted lists、escaped strings、characters、vectors、bytevectors；
- quote/quasiquote/unquote/unquote-splicing；
- datum labels 和循环/共享 datum；
- source-located reader errors；
- hygienic `syntax-rules`、ellipsis、syntax scopes；
- derived forms；
- library `only`、`except`、`prefix`、`rename`、`include`、`include-ci`、`cond-expand`；
- native full compiler 和 persistent REPL 共用 frontend。

明确缺口：

- 当前字符/string repertoire 是 ASCII 基线，不是完整 Unicode；
- 数字 reader/printer 不能被视为完整 number tower 语义；
- public eval 的 core syntax visibility 仍不是精确 import-set filtering；
- reader/writer 的完整 R7RS edge cases 尚未逐项 inventory close。

正确处理方式是扩展已有实现，不能另写 evaluator reader、宏系统或 library resolver。

### 5.3 ports、I/O 和 process 基线

状态：`existing-partial`。

已有能力：

- textual/binary port representation；
- current input/output/error port；
- open/close/flush；
- textual/binary reads and writes；
- EOF；
- string ports and bytevector ports；
- file open/delete/exists primitives；
- command-line、exit、environment access；
- native port/binary-I/O/string-I/O/standard-I/O/process tests。

明确缺口：

- `(scheme file)` 的四个 wrapper 未导出；
- 完整 call-with-port、异常关闭和所有错误类别需要逐项确认；
- Unicode text semantics 未完成；
- 端口生命周期和资源行为尚未通过统一 native/REPL/Guile fixture 关闭。

因此 B3 不是“从零实现”，也不是“完整完成”；后续只实现实际缺口。

## 6. 当前库导出差异

### 6.1 `(scheme base)` 缺失导出

以下名称在 R7RS base summary 中存在，但当前 `lib/scheme/base.sld` 未导出：

```text
... / => _ abs apply assoc assq assv begin boolean=? boolean?
bytevector-append bytevector-copy bytevector-copy! caar cadr call-with-port
cdar cddr ceiling char-ready? char<=? char<? char=? char>=? char>?
complex? cond-expand define define-record-type define-syntax define-values
denominator else equal? error error-object-irritants error-object-message
error-object? even? exact exact-integer-sqrt exact-integer? exact? expt
features file-error? floor floor-quotient floor-remainder floor/ for-each gcd
if include include-ci inexact inexact? integer? lambda lcm let let*
let-syntax let-values letrec letrec* letrec-syntax list->string list-copy
list-ref list-set! list-tail make-bytevector make-list make-string make-vector
map max member memq memv min modulo negative? numerator odd? positive?
procedure? quasiquote quote quotient rational? rationalize read-error?
read-line read-string real? remainder round set! square string string->list
string->number string->symbol string->utf8 string->vector string-copy
string-copy! string-fill! string-for-each string-map string-set!
string<=? string<? string>=? string>? string? symbol=? syntax-error
syntax-rules truncate truncate-quotient truncate-remainder truncate/
u8-ready? unquote unquote-splicing utf8->string vector->list
vector->string vector-append vector-copy vector-copy! vector-fill!
vector-for-each vector-map write-string zero?
```

注：当前 base 还导出了 `delay`、`delay-force`、`force`、`make-promise`、`promise?`。这些名称属于 `(scheme lazy)`，在本地 base export surface 中属于 `wrong-surface`，即使底层 promise 语义已有实现，也不能直接算作 R7RS base 正确。

### 6.2 `(scheme char)` 差异

缺失：

```text
string-ci<=? string-ci<? string-ci=? string-ci>=? string-ci>?
string-downcase string-foldcase string-upcase
```

当前 char library 额外导出 case-sensitive：

```text
char=? char<? char>? char<=? char>=?
```

这些应与 R7RS base/char 分层重新核对，但不能因为导出位置错误就重写已有字符比较实现。

### 6.3 其他缺失

```text
(scheme complex): angle imag-part magnitude make-polar make-rectangular real-part
(scheme inexact): acos asin atan cos exp finite? infinite? log nan? sin sqrt tan
(scheme time): current-jiffy current-second jiffies-per-second
(scheme file): call-with-input-file call-with-output-file
                 with-input-from-file with-output-to-file
(scheme r5rs): complete compatibility export layer
```

## 7. 当前测试证据与证据边界

### 已有直接测试族

```text
Dynamic Core
Reader/Datum
Macros/Derived Forms
Explicit Control
Multiple Values
Continuations
Dynamic Wind
Exceptions
Guard
Parameters
Promises
Libraries
Eval
Load
Ports
Standard I/O
Binary I/O
String I/O
Process
Persistent REPL
```

### 不能从现有测试推出的结论

现有测试不能推出：

- 完整 `(scheme base)` 已实现；
- 完整 Unicode 已实现；
- 完整 number tower 已实现；
- `(scheme complex)`、`(scheme inexact)`、`(scheme time)`、`(scheme r5rs)` 已实现；
- `(scheme load)` 两参数形式已实现；
- native 与 persistent REPL 对所有标准库行为严格一致；
- 与 Guile 的差异已经被系统分类；
- benchmark 已建立或结果可比较。

## 8. 审计后的真实依赖关系

### 已完成基线

```text
现有控制/异常/动态状态/multiple-values
现有 ASCII reader/datum/macro/library frontend
现有 ports/I/O/process substrate（均为 partial 或 surface-limited）
现有 promises
现有 native compiler 和 persistent REPL 验证入口
```

### 真正需要新增的主线

```text
A：逐项 inventory + 最小 shared native/REPL/Guile fixture runner
  |
  +--> 数值表示和 number tower
  |      |
  |      +--> numeric reader/printer completion
  |
  +--> Unicode character/string/UTF-8
  |      |
  |      +--> Unicode reader/writer completion
  |
  +--> ports/file wrapper/error/resource gaps
         |
         +--> complete base export surface
                       |
                       +--> complex/inexact/char/cxr/case-lambda/read/write/
                       |    process-context/time/lazy 的逐项缺口收尾
                       |
                       +--> eval crossing gaps
                                  |
                                  +--> repl interaction contract
                                             |
                                             +--> load environment-specifier
                                                        |
                                                        +--> r5rs compatibility exports
```

其中控制/异常/multiple-values 不在新增主线中；只作为已实现依赖被后续 fixture 使用。

### 重要顺序修正

- number tower 不能再被笼统地叫作“base 之后的库”；base 的数值导出依赖它；
- Unicode 不能只放在 char library 末尾，因为 string、reader、writer 和 UTF-8 都依赖它；
- ports 已有实现，不应重新建立 B3；只需先清点 wrapper、错误和资源缺口；
- reader/datum 已有实现，不应重新建立 B4；只扩展最终 numeric/Unicode/port 缺口；
- eval -> repl -> load 仍然是顺序依赖；load 的第二参数不能用未经审核的临时协议；
- r5rs 必须最后生成，并且只能导出已完成的语义。

## 9. 审计结论

1. 控制、异常、动态状态和 multiple values 已有实现和测试，**不重做**。
2. 当前标准库整体是部分实现，不是完整 R7RS-small。
3. 缺失的主要公共库是 complex、inexact、time、r5rs；file、base、char、eval、load 有明确缺口。
4. B1/B3/B4 不能作为“从零开始的实现阶段”；它们应改为现有基线审计 + 具体缺口修复。
5. 下一步不是开始 number tower，而是完成最小的 A2 shared fixture runner，并据此把当前表面差异和语义差异分开记录。
6. 在 A2 完成、审计结果被用户确认前，不开始新的标准库实现。
