# syntax-rules 省略号宽松匹配与 lambda 简写调研（2026-11-14）

- **起因**：用户在 npli 中定义 `\` 宏（`x y => body` 形式的 lambda 简写），
  `((\ (x . rest) => (list x rest)) 1 2 3 4)` 报
  `error: formal name must be an identifier`。
- **性质**：只读调研与探针实测，未修改仓库源码。
- **基线**：`feature/0.1.2-i3-remaining` @ `2f68c6b`。`src/compiler/frontend`、
  `src/compiler/reader`、`runtime`、`lib/scheme` 自 `8b82f79` 起无改动；
  `build/bin/npli` 构建于 HEAD 之后，nplc 由 `bootstrap/current.ssa` 现建。
- **对照**：Guile（`guile --no-auto-compile`）与 Racket 9.2（`#lang racket/base`）。

## 结论

1. 用户的报错是**宏的写法问题**，不是匹配器的 bug：规则按顺序尝试，规则 2
   `(_ arg ... => body ...)` 的 `arg` 能匹配任意形式，`(x . rest)` 在规则 2
   就被匹配，展开为 `(lambda ((x . rest)) ...)`。规则 3、4 永远到不了。
2. 规则 2 本身不是合法的 R7RS 模式（同层两个省略号）。NPL 接受它，Guile 和
   Racket 都在定义时拒绝。这是 NPL 比 R7RS 宽松的地方，见 N1。
3. 只有一个 `=>` 时，NPL 宽松模式下的匹配结果符合直觉；多个 `=>` 时，最后一个
   生效，参数里的 `=>` 会被**静默当成形参名**。
4. 严格 R7RS 下这种简写可以用“逐个收集”的内部规则实现，柯里化和拒绝多个 `=>`
   也都能做，见“推荐写法”。

## NPL 问题

### N1【中 · 一致性】syntax-rules 接受同层多个省略号，语义未文档化

- **位置**：`src/compiler/frontend/syntax.npl:310` `validate-pattern-list`
  不检查同层第二个省略号；`syntax.npl:555` `match-repeated-pattern` 先贪婪
  消费再回溯，因此 `(_ arg ... => body ...)` 能匹配，`=>` 切在**最后一个**
  出现处。
- **证据**：

  | 输入 | NPL | Guile / Racket |
  |---|---|---|
  | 定义 `(_ arg ... => body ...)` | 接受 | `misplaced ellipsis` / `follows other ellipsis`，定义时拒绝 |
  | `(probe a => b => c)` | `arg` = `(a => b)`，`body` = `(c)` | — |
  | 原宏 `(\ x => y => (+ x y))`，调用 `(f 1 'ignored 2)` | 展开为 `(lambda (x => y) ...)`，得 `3` | — |

- **影响**：在 NPL 能用的宏移植到其他实现会失败；未文档化的回溯语义没有稳定性
  保证；用户的宏遇到多个 `=>` 不报错，而是静默生成错误的参数列表（`=>` 是合法
  标识符，`(lambda (x => y) ...)` 本身合法，这不是 lambda 的问题）。
- **收紧的代价**：用 [`scan_ellipsis.py`](probes/2026-11-14-syntax-rules-ellipsis/scan_ellipsis.py) 扫描了 `lib/`、`src/`、`tests/` 中 435 个
  `.scm/.sld/.npl` 文件（47 个使用 `syntax-rules`），没有同层多省略号的模式。
  唯一命中是 `src/compiler/tests/macro-scopes.npl:24` 的 `(... ...)` 转义，属于
  误报。按 R7RS 收紧不会破坏仓库现有代码。
- **建议**（需要决策）：
  - A. 按 R7RS 收紧：在 `validate-pattern-list` 中拒绝同层第二个省略号，报
    `misplaced ellipsis in pattern`，并加负向测试。
  - B. 保留为扩展：在 `docs/r7rs-conformance.md` 写明“同层多省略号、贪婪加
    回溯、最后一个分隔符生效”，并加正向测试固定该语义。
  - 倾向 A。需要回溯匹配时，另行提供类似 Racket `syntax-parse` 的匹配器，
    支持语法类与否定模式（见“Racket 调研”），而不是让 `syntax-rules` 保持
    隐式宽松。

### N2【低 · 双路径不一致】`\` 作为裸标识符：npli 接受，nplc 拒绝

- **位置**：nplc 词法分析器 `src/compiler/reader/lexer.npl:444` `do-bad-char`
  以 `nplc: bad character byte 92` 退出（exit 20），不带行列号；npli 使用
  `runtime/read.c`，把 `\` 读成符号。
- **规范**：R7RS 标识符的首字符集合不含 `\`；`\` 只能出现在 `|...|` 内作转义。
  nplc 的行为符合规范。其他实现不统一：Guile 接受 `\` 作标识符（探针 08 输出
  `1`），Racket 把它当转义符。
- **建议**：让 `runtime/read.c` 同样拒绝（或两侧都作为扩展接受并文档化），在
  reader 差分 fixture 中加一例；nplc 的报错补上行列号（与
  `paren-close-error` 一致）。

### N3【信息 · 待确认】无 import 环境缺少 `assv`、`assoc`

- 不写 `(import (scheme base))` 时，npli 报 `unbound identifier: assv`，nplc 报
  `unbound symbol assv`；`assoc` 同样。两条路径一致，加上 import 后正常。
- `docs/toolchain.md:153` 提到 import-free 环境，说明该环境是有意的子集，但未列出
  包含哪些绑定。建议在文档中写明范围，或确认这两个过程是否应该在内。

## 推荐写法

以下写法均实测通过。宏名为 `\` 的版本只能在 npli 中使用（见 N2），nplc 与
Racket 测试时改名为 `fn`。

### 严格 R7RS（可移植）

```scheme
(define-syntax \
  (syntax-rules (=>)
    ((_ "collect" (arg ...) => body ...) (lambda (arg ...) body ...))
    ((_ "collect" (arg ...) next rest ...) (\ "collect" (arg ... next) rest ...))
    ((_ => body ...) (lambda () body ...))
    ((_ () => body ...) (lambda () body ...))
    ((_ (arg0 arg ... . rest) => body ...) (lambda (arg0 arg ... . rest) body ...))
    ((_ arg0 rest ...) (\ "collect" (arg0) rest ...))))
```

- 第一个 `=>` 处切分；`(arg0 arg ... . rest)` 要求括号形式至少有一个元素，
  避免裸标识符被 `rest` 吞掉成为可变参数 lambda。
- npli 与 Guile 输出相同：`(0 3 7 (1 (2 3 4)) 12 empty (3 2 1))`；Racket 改名后
  输出相同。

### 柯里化（依赖 N1 的宽松匹配）

```scheme
(define-syntax \
  (syntax-rules (=>)
    ((_ "params" (arg ...) => form ...) (\ "body" (arg ...) form ...))
    ((_ "params" (arg ...) next more ...) (\ "params" (arg ... next) more ...))
    ((_ "body" formals form ... => more ...) (lambda formals (\ form ... => more ...)))
    ((_ "body" formals form ...) (lambda formals form ...))
    ((_ => form ...) (\ "body" () form ...))
    ((_ () => form ...) (\ "body" () form ...))
    ((_ (arg0 arg ... . rest) => form ...) (\ "body" (arg0 arg ... . rest) form ...))
    ((_ arg0 more ...) (\ "params" (arg0) more ...))))
```

- `(\ x => y z => w => body)` 展开为
  `(lambda (x) (lambda (y z) (lambda (w) body)))`。只有 body 顶层的 `=>` 是
  分隔符，`(cond (... => cdr))` 不受影响。
- `"body"` 规则贪婪切在最后一个 `=>`，但递归回去的部分按第一个 `=>` 重新切，
  最终等价于从左到右切分。
- npli（带 import）与 nplc 输出均为 `(3 10 (1 (2 3) 4) 5 36 one)`，覆盖多段
  柯里化、rest 参数、空参数段、多表达式 body、带 `=>` 的 `cond`。Guile 在定义时
  拒绝（同层多省略号）。若 N1 选择 A，需要把 `"body"` 规则改成逐个扫描 `=>` 的
  辅助规则。

### 拒绝多个 `=>`

把柯里化版本的 `"body"` 第一条替换为：

```scheme
    ((_ "body" formals form ... => more ...)
     (syntax-error "\\: only one => is allowed" form ... => more ...))
```

npli 与 nplc 均在展开时报 `only one => is allowed`；单个 `=>` 正常返回 `3`。

## Racket 调研

| 方式 | 机制 | 是否实测 |
|---|---|---|
| curried define：`(define ((adder n) m) (+ n m))` | 核心 `define` 支持 | 是，`((adder 1) 2)` → `3` |
| `curry` / `curryr`（`racket/function`） | 运行时部分应用，不是语法柯里化 | 是，`((curry + 1) 2)` → `3` |
| `syntax-parse` 宏 | 回溯匹配器：同层多省略号、语法类 `x:id`、否定模式 `~not`、`...+`，自动生成定位到子形式的报错 | 是，见下 |
| `fancy-app` | 重定义 `#%app`，`(+ _ 1)` 成为匿名函数 | 否（未安装） |
| `afl` / `rackjure` | 扩展读取器，`#λ(+ % 1)` | 否（未安装） |
| `threading` | `~>` / `~>>` 管道宏 | 否（未安装） |
| 自定义 `#lang` | 替换读取器与语法 | 否 |

Racket 的 `syntax-rules` 与 R7RS 一样严格，同层多省略号由 `syntax-parse` 负责：

```racket
(define-syntax (fn stx)
  (syntax-parse stx
    #:datum-literals (=>)
    [(_ (~and x:id (~not =>)) ... => body ...+ => more ...+)
     #'(lambda (x ...) (fn body ... => more ...))]
    [(_ (~and x:id (~not =>)) ... => body ...+)
     #'(lambda (x ...) body ...)]
    [(_ (x:id ... . rest) => body ...+)
     #'(lambda (x ... . rest) body ...)]))
```

- 实测：`((fn x y => (+ x y)) 1 2)` → `3`，`(((fn x => y => (+ x y)) 1) 2)` → `3`，
  `((fn (x . r) => (list x r)) 1 2 3)` → `(1 (2 3))`。
- `(~and x:id (~not =>))` 在模式层排除了“`=>` 成为形参”的情况；不匹配时报
  `fn: expected identifier or expected the literal symbol '=>'`，并指出
  `at: (x . r)`。
- Racket 读取器把 `\` 当转义符，`\ =>` 读成符号 `| =>|`，所以 Racket 测试必须
  改名。

## 探针

探针在 [`probes/2026-11-14-syntax-rules-ellipsis/`](probes/2026-11-14-syntax-rules-ellipsis/)，
用 [`probes/run.sh`](probes/run.sh) 运行，探针的实测输出见
`observed.txt`（文件头记录运行时的 commit），对应关系见 [`probes/README.md`](probes/README.md)。

## 后续动作

1. 决定 N1 采用 A（收紧）还是 B（文档化扩展），并补对应的正向或负向测试。
2. 统一 N2 的两侧读取器行为，补 reader 差分用例和 nplc 报错行列号。
3. 确认 N3 的 import-free 环境范围并写进文档。
4. 若要支持回溯匹配，单独设计 `syntax-parse` 风格的匹配器，复用
   `syntax.npl` 的 marks 与捕获结构（AGENTS.md 复用原则），作为后续版本的
   计划项，不并入 0.1.2。

## 局限

- 多省略号扫描是基于 token 的启发式脚本，未覆盖 `define-syntax` 以外由
  宏生成的 `syntax-rules`，也不识别自定义省略号标识符。
- 第三方 Racket 包未安装，表中相应行凭文档记忆，未实测。
- 未检查 nplc 与 npli 在 N1 贪婪回溯语义上是否完全一致；两者共用
  `syntax.npl`，柯里化用例输出一致，但没有专门的差分用例。
