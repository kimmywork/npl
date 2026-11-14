# 宏系统深入审查与 syntax-case / syntax-parse 必要性评估（2026-11-14）

- **范围**：`syntax-rules` 匹配与转写（`src/compiler/frontend/syntax.npl`）、
  卫生转写（`transcription.npl`）、展开器的 body/定义处理（`expand.npl`）、
  库导入对核心语法的处理。npli 与 nplc 共用这套前端。
- **性质**：只读审查加探针实测，未修改源码。
- **基线**：`feature/0.1.2-i3-remaining` @ `2f68c6b`；工作区的未提交改动不涉及
  `src/compiler/frontend`。nplc 由当前 `bootstrap/current.ssa` 现建，npli 为
  `build/bin/npli`（构建于 HEAD 之后）。
- **对照**：Guile 3（`guile --no-auto-compile`）。
- **前置文档**：[syntax-rules 省略号宽松匹配](2026-11-14-syntax-rules-ellipsis-and-lambda-shorthand.md)
  （N1-N3）。本文问题编号为 M1 起。

## 结论

1. 常规卫生用例表现良好：29 个探针中，swap/`tmp`、局部重绑定 `if`/`else`、
   嵌套省略号、向量模式、`(... ...)` 转义、自定义省略号、`let-syntax` 与
   `letrec-syntax` 作用域、宏定义宏的字面量、body 内宏引入的定义、带点尾部模式、
   引用中的符号、宏关键字被局部变量遮蔽，两条路径都与 Guile 一致。
2. 但有两个**影响普通用户代码**的高严重度问题：宏展开出单个 `define` 时，nplc
   会拒绝或**误编译**（M1，master 上已存在）；深度 0 的模式变量不能出现在省略号
   模板里，这在 R7RS 中是合法且常见的写法（M2）。
3. 卫生模型有一个结构性缺陷（M3）：复制模板标识符时只保留新 mark，丢失了原
   mark，alias 层只能按名字找绑定，宏引入的同名绑定会捕获用户标识符。
4. **syntax-case 与 syntax-parse 目前都没有必要实现。** 路线图里没有依赖它们的
   项目，前提条件（编译期求值、能保留上下文的标识符表示、源码位置）也都不具备。
   先修好 `syntax-rules`；若之后需要过程宏，先评估 SRFI 211 的
   `er-macro-transformer`/`ir-macro-transformer`，它们可以复用现有的标识符模型。

## 问题

### M1【高 · nplc 误编译 / 双路径不一致】宏展开出的单个 `define` 不进入 body 作用域

- **位置**：`expand.npl:69` `expand-transcription` 只把 `define-syntax`、
  `define-values` 和含定义的 `begin` 当作定义输出原样返回；单个 `define` 被当作
  表达式展开，其目标名在 `expand-define` 中按当前环境解析，而当前环境里没有它。
  `expand.npl:385` 的 `(raw-form? expanded "define")` 分支也不扩展 `value-env`。
  定义名因此不经过 alpha 重命名，只能靠原始名字碰巧对上。
- **证据**（`def` 为 `(syntax-rules () ((_ n) (define (n) 'inner)))`）：

  | 程序 | nplc | npli | Guile |
  |---|---|---|---|
  | `(define (f) (def get) (get))` | `unbound identifier: get`（编译失败） | `v` | `v` |
  | 同上，外层另有全局 `(define (get) 'outer)` | 运行时 `wrong number of arguments: -1` | `(inner outer)` | `(inner outer)` |
  | `(define n 'inner)` 形式，外层有全局 `x` | `(inner outer)` | 同 | 同 |

  第二行是静默误编译。master（`c05dd68` 的种子）上结果相同，不是本分支引入。
- **建议**：在 `expand-transcription` 中把单个 `define` 与 `define-values` 同样
  处理（返回 `(begin ...)` 供拼接），由 `expand-body-sequence` 的拼接分支统一绑定。
  加 nplc/npli 差分用例，覆盖过程形式、变量形式，以及与外层同名的情形。

### M2【高 · R7RS 一致性】深度 0 的模式变量不能出现在省略号模板中

- **位置**：`syntax.npl:686` 要求变量深度**等于**当前模板路径长度，否则报
  `pattern variable used at wrong ellipsis depth`。
- **规范**：R7RS 4.3.2 只要求“出现在省略号后的子模式中的模式变量”必须出现在
  同样多省略号的子模板中；深度 0 的变量不受限制。
- **证据**：`((_ f (x ...)) (list (f x) ...))` 和
  `((_ a (b ...)) '((a b) ...))` 在 npli/nplc 都报错；Guile 分别输出 `(-1 -2 -3)`
  和 `((k 1) (k 2) (k 3))`。现有负向用例 `macro-errors/wrong-ellipsis-depth.npl`
  只覆盖相反方向（深度 1 的变量用在深度 0），所以没有发现这个问题。
- **建议**：允许变量深度小于路径长度，按路径的前缀（长度等于变量深度）查找
  capture；`template-repeat-count` 已只统计驱动深度的变量，无需改动。补正向用例。

### M3【中 · 卫生】alias 层按任意 mark 查找，宏引入的绑定会捕获用户标识符

- **位置**：`syntax.npl:72` 与 `:83` `lookup-value-binding-any-mark`。模板标识符
  被复制时（`syntax.npl:683`）只带新 mark；查找时，alias 层在定义环境中取该名字
  **最内层的任意 mark 绑定**。
- **证据**：

  ```scheme
  (define tmp 'outer)
  (define-syntax with-tmp (syntax-rules () ((_ body) (let ((tmp 'inner)) body))))
  (with-tmp (let-syntax ((m (syntax-rules () ((_) tmp)))) (m)))
  ```

  `m` 模板里的 `tmp` 是用户写的，应指向全局：Guile 得 `outer`，npli/nplc 都得
  `inner`。
- **根因**：标识符只有一个 mark，复制时丢失了原 mark，alias 层无法区分定义环境中
  用户写的绑定和宏引入的同名绑定。
- **建议**：复制时记录每个新标识符的源 mark（即 mark 历史），alias 层用源 mark
  在定义环境中查找，不再按任意 mark。这也是将来做 `datum->syntax` 等操作的前提
  （见“syntax-case 评估”）。

### M4【中 · 双路径不一致】顶层宏引入的定义

- `(define-syntax def-getter (syntax-rules () ((_ name val) (begin (define tmp val) (define (name) tmp)))))`，
  在顶层调用两次：
  - npli 共享同一个 `tmp`，`(list (get-a) (get-b))` 得 `(2 2)`；
  - nplc 报 `duplicate top-level definition: tmp`；
  - Guile 对引入名做重命名，得 `(1 2)`。
- R7RS 没有规定顶层的行为，但 AGENTS.md 要求两条路径一致。body 内的同样用法
  两条路径都正确，得 `(1 2)`。
- **建议**：决定一种语义（像 Guile/Racket 那样重命名，或两条路径都拒绝）并加
  差分用例。重命名更符合卫生直觉。

### M5【中 · R7RS 5.3】body 内后定义的宏不能被前面定义的 lambda 体使用

- `(define (f) (define (g) (m)) (define-syntax m (syntax-rules () ((_) 42))) (g))`：
  npli/nplc 报 `syntax keyword used before its definition`，Guile 得 `42`。
- R7RS 只禁止“确定某个定义的含义需要用到该绑定”的情况；`g` 的 lambda 体不影响
  `g` 这个定义的含义。现有负向用例 `internal-use-before-definition.npl` 把宏用在
  定义的初始化表达式里，按规范属于错误，应保留。
- **建议**：采用标准的两遍 body 展开：第一遍只展开到能识别定义边界为止，先处理
  所有 `define-syntax`；第二遍再展开 lambda 体和初始化表达式。

### M6【中 · R7RS 一致性】核心语法没有从 `(scheme base)` 导出

- `(import (only (scheme base) if))` 报 `only import name not exported`；`except`、
  `rename` 同样报错。
- `(import (prefix (scheme base) b:))` 后，`(b:if #t (write 'yes) (write 'no))` 在
  npli 中**先求值了两个分支**（输出 `yesno`），然后才报 `unbound identifier: b:if`。
- 原因：核心形式在 `expand-keyword-or-application`（`expand.npl:168`）中按名字
  识别，不是库导出的绑定。
- **建议**：把核心语法关键字作为 `(scheme base)` 的导出绑定参与导入集运算；至少
  在 `docs/r7rs-conformance.md` 中注明这个限制。

### M7【低】模板的省略号深度错误要到使用时才报

- `((_ x ...) (list x))` 在 npli/nplc 中能定义成功，只在使用时报错；Guile 在定义
  时报 `missing ellipsis`。
- **建议**：`validate-rule` 已经算出了模式变量的深度，可以在定义时顺带检查模板。

### M8【低 · 性能】大省略号的转写是平方复杂度

- `(my-list x ...)` 传入 16,000 个元素，nplc 用时 2.03s，同样大小的普通
  `(list ...)` 用时 0.17s；4,000 个元素时分别是 0.24s 和 0.14s。
- **位置**：capture 存在线性列表里，`capture-exact`（`syntax.npl:457`）和
  `capture-next-count`（`:467`）每次都扫描整个列表。
- **建议**：按 `(名字, 路径)` 用哈希表索引 capture。不急，但在用宏生成数据表时
  会遇到。

### M9【低 · 诊断，已在 D1 中】宏错误没有位置和形式信息

- `macro use does not match any syntax rule: two`、`only-ids: bad form`（来自
  `syntax-error`）都不带源码位置，也不显示出错的形式和参数；Guile 会给出文件、
  行列和完整形式。

### M10【信息】未支持的 R7RS 之外的扩展

- 模板中的 `(a ... ...)`（压平）和“深度 1 的变量出现在两层省略号下”，Guile 都
  支持，NPL 报错。这些属于 SRFI 149（syntax-rules 模板扩展），不是 R7RS-small 的
  要求。实现成本低，可以与 M2 一起考虑。

## syntax-case / syntax-parse 评估

### 需求面

- `docs/r7rs-conformance.md:117` 与 workstate 记录：`syntax-case` 已由用户决定
  延后，并且延后期间不希望实现。
- 路线图中没有依赖它的项目：`docs/reference/` 的 SRFI 文本里，只有 SRFI 226
  （`guard` 的示例实现）和 SRFI 41（`stream-match` 的参考实现）用到了
  `syntax-case`；SRFI 41 已经用 `syntax-rules` 移植完成；完整的 SRFI 226 在
  0.1.3 计划中。
- 实际遇到的宏需求（如 lambda 简写）用 `syntax-rules` 都能写（见前置文档）。
  当前的痛点是 `syntax-rules` 本身的缺陷（M1-M6），不是表达能力不够。

### 实现 syntax-case 的前提与代价

1. **编译期求值**：nplc 是整程序编译到 QBE 的编译器，转换器是任意代码，必须在
   编译时执行。需要把求值器（`lib/scheme/internal/evaluator-*.scm`）链接进 nplc，
   并在编译期加载转换器依赖的库（相当于 R6RS 的 phase）。这会增大种子并影响
   relay 成本。
2. **标识符表示**：现在的标识符是“符号对象 + 全局 eq 表里的单个 mark + 按 mark
   分层的 alias 环境”。`datum->syntax`、`free-identifier=?`、`bound-identifier=?`
   要求标识符携带完整的词法上下文，M3 已经说明当前模型会丢失 mark 历史，需要
   换成 sets-of-scopes 或 marks + substitutions（psyntax）。这是对两条路径共享
   核心的重写。
3. **源码位置**：syntax 对象应该带位置，否则过程宏的报错比现在更难定位，而 D1
   的源码位置还没有完成。
4. **双路径一致性**：npli 与 nplc 都必须支持，并加差分用例。

### syntax-parse

依赖上面全部基础设施，还需要语法类、带进度的回溯错误报告和源码位置。Racket
的实现规模很大，与 NPL 当前阶段不匹配。**不建议实现。** 需要的能力（约束形式、
更好的报错）可以通过修好 `syntax-rules` 的诊断（M7、M9）部分获得。

### 建议顺序

1. 修 M1、M2（影响普通代码，改动局部）。
2. 修 M3、M5，决定 M4、M6 与 N1 的语义；补差分用例。
3. D1 的源码位置一并覆盖宏错误（M9）。
4. 可选：SRFI 149 模板扩展（M10）。
5. 确实需要过程宏时，先做 SRFI 211 的 `er-macro-transformer` /
   `ir-macro-transformer`：仍需编译期求值（第 1 条），但 `rename` 可以复用现有的
   alias 机制，`compare` 可以复用 `literal-identifier-match?`，不需要重写标识符
   模型。`syntax-case` 放在 M3 所述的标识符模型重建之后再评估。

## 探针清单

探针在 [`probes/2026-11-14-macro-system/`](probes/2026-11-14-macro-system/)：
`cases/` 下 29 个原始用例和 3 个导入集用例（31-33），`body-define/` 下 M1 的定位
用例，`scale/gen.py` 生成 M8 的规模用例。用 [`probes/run.sh`](probes/run.sh) 运行，
探针的实测输出见 `observed.txt`（文件头记录运行时的 commit），对应关系见
[`probes/README.md`](probes/README.md)。修复时应转成 `src/compiler/tests/` 与
`tests/r7rs/fixtures/` 中的正式用例。

| 结果 | 用例 |
|---|---|
| 三方一致 | swap、`if`/`else` 遮蔽、嵌套省略号、向量模式、`(... ...)`、自定义省略号、body 内引入定义、`let-syntax`/`letrec-syntax`、带点尾部、引用符号、向量模板、字面量数据、宏定义宏的字面量、递归展开、宏被变量遮蔽、表达式输出的 alias 查找 |
| 双路径一致，未与 Guile 对比 | body 内宏展开出 `define-record-type`（Guile 需要 SRFI 9） |
| NPL 与 Guile 不同 | M1、M2、M3、M4、M5、M7、M10 |
| NPL 双路径不同 | M1（nplc 失败或误编译，npli 正确）、M4 |
| 仅 nplc 失败 | `make-definer` 一例（宏定义的宏在 body 内展开出过程定义），根因同 M1 |

## 局限

- 未运行完整门禁；结论来自探针，未改代码。
- M3 只验证了 `let-syntax` 表达式输出的路径；拼接定义的路径因为 M1 无法单独
  验证，修好 M1 后应复测。
- 平方复杂度只测了 nplc 的展开加代码生成时间，没有单独剖析展开阶段。
- 没有与 Racket 和 chibi 逐项对照；Guile 的个别行为（如 M4 的重命名）属于实现
  定义。
