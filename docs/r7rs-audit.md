# R7RS-small 合规审计(nplc0/nplc1 当前实现 vs 标准)

> 数据来源:`docs/reference/r7rs.html`(R7RS-small 官方 HTML,全库条目
> 头部机械化抽取)。R7RS-small 共定义 **319 个独立标识符**(base 库
> ≈239,其余分布在 char/inexact/file/lazy/cxr 等 14 个标准库)。
> 与 stage0/nplc1 当前 emit 面比对:直接可用约 42 个。下列分组是
> 标识符盘点与落地成本分类；具体实现状态以“当前实现覆盖”一节为准。
>
> **审计结论执行口径**:DESIGN.md §1.1 的目标一直是"R7RS-small 兼容核心 +
> 自举子集",不是 100% base;§6.2 明确列出 stage0/自举所需的最小子集。
> 本报告把差距按"落地成本"分级(C→E→F→D→G),并标注每项的先决条件。

## A-nplc1内建 — 25

- base(23): *, +, -, <, <=, >, >=, car, cdr, cons, eq?, newline, not, null?, number->string, pair?, set-car!, set-cdr!, string-append, string-length, string=?, substring, symbol->string
- (process-context) 1: exit
- (write) 1: display

## B-stage0 direct — 17

- base(17): /, =, and, begin, call-with-current-continuation, call/cc, cond, define-syntax, else, if, lambda, let, number?, or, quote, set!, string-ref

## C-macro易(syntax-rules可写) — 5

- base(5): case, cond-expand, do, unless, when

当前实现覆盖:
- nplc1 已直接支持 `when`/`unless`。
- nplc1 的 `case` 为已验证子集:数值/symbol datum list、`else`、顺序 clause body；
  尚无 `=>` clause，datum 类型受自举 reader 限制，因此不标记为完整 R7RS `case`。
- `cond-expand`/`do` 尚未接入 nplc1。

## D-macro需ellipsis — 3

- base(3): quasiquote, unquote, unquote-splicing

## E-special-form增补 — 3

- base(3): let*, letrec, letrec*

当前实现覆盖:
- nplc1 的 `let*` 已通过顺序绑定回归。
- `letrec`/`letrec*` 已可解析和执行简单值绑定，但 emitter 仍复用顺序
  `emit-binds`；未实现完整 letrec location 预分配与相互递归初始化语义，标记为 partial。

## F-libNPL/NPL层库 — 67

- base(67): abs, append, assoc, assq, assv, boolean?, ceiling, char<?, char=?, char>?, even?, exact, exact-integer-sqrt, exact?, expt, floor, floor-quotient, floor-remainder, floor/, for-each, gcd, inexact, inexact?, integer?, lcm, length, list, list->string, list-copy, list-ref, list-set!, list-tail, make-list, map, max, member, memq, memv, min, modulo, negative?, odd?, positive?, quotient, rational?, real?, remainder, reverse, round, square, string->list, string-copy, string-for-each, string-map, truncate, truncate-quotient, truncate-remainder, truncate/, vector-append, vector-copy, vector-fill!, vector-for-each, vector-length, vector-map, vector-ref, vector-set!, zero?

## G-大特性 — 11

- base(10): define-library, define-record-type, define-values, include, include-ci, let*-values, let-syntax, let-values, letrec-syntax, syntax-error
- (case-lambda) 1: case-lambda

## C-runtime(C函数) — 188

- base(111): ..., =>, _, apply, binary-port?, boolean=?, bytevector, bytevector-append, bytevector-copy, bytevector-copy!, bytevector-length, bytevector-u8-ref, bytevector-u8-set!, bytevector?, caar, cadr, call-with-port, call-with-values, cdar, cddr, char->integer, char-ready?, char<=?, char>=?, char?, close-input-port, close-output-port, close-port, complex?, current-error-port, current-input-port, current-output-port, denominator, dynamic-wind, eof-object, eof-object?, equal?, eqv?, error, error-object-irritants, error-object-message, error-object?, exact-integer?, features, file-error?, flush-output-port, get-output-bytevector, get-output-string, guard, input-port-open?, input-port?, integer->char, list->vector, list?, make-bytevector, make-parameter, make-string, make-vector, numerator, open-input-bytevector, open-input-string, open-output-bytevector, open-output-string, output-port-open?, output-port?, parameterize, peek-char, peek-u8, port?, procedure?, raise, raise-continuable, rationalize, read-bytevector, read-bytevector!, read-char, read-error?, read-line, read-string, read-u8, string, string->number, string->symbol, string->utf8, string->vector, string-copy!, string-fill!, string-set!, string<=?, string<?, string>=?, string>?, string?, symbol=?, symbol?, syntax-rules, template, textual-port?, u8-ready?, utf8->string, values, vector, vector->list, vector->string, vector-copy!, vector?, with-exception-handler, write-bytevector, write-char, write-string, write-u8
- (char) 22: char-alphabetic?, char-ci<=?, char-ci<?, char-ci=?, char-ci>=?, char-ci>?, char-downcase, char-foldcase, char-lower-case?, char-numeric?, char-upcase, char-upper-case?, char-whitespace?, digit-value, string-ci<=?, string-ci<?, string-ci=?, string-ci>=?, string-ci>?, string-downcase, string-foldcase, string-upcase
- (inexact) 12: acos, asin, atan, cos, exp, finite?, infinite?, log, nan?, sin, sqrt, tan
- (file) 10: call-with-input-file, call-with-output-file, delete-file, file-exists?, open-binary-input-file, open-binary-output-file, open-input-file, open-output-file, with-input-from-file, with-output-to-file
- (complex) 6: angle, imag-part, magnitude, make-polar, make-rectangular, real-part
- (lazy) 5: delay, delay-force, force, make-promise, promise?
- (cxr) 4: caaar, caadr, cdddar, cddddr
- (process-context) 4: command-line, emergency-exit, get-environment-variable, get-environment-variables
- (time) 3: current-jiffy, current-second, jiffies-per-second
- (write) 3: write, write-shared, write-simple
- (eval) 2: environment, eval
- (r5rs) 2: null-environment, scheme-report-environment
- (load) 1: load
- (name) 1: template
- (read) 1: read
- (repl) 1: interaction-environment

## 结论

| 状态 | 数量 | 占比(总 319 标识符) |
|------|------|------|
| A. nplc1 内建可直接发射 | 25 | ~8% |
| B. stage0 direct | 17 | ~5% |
| C. syntax-rules 可派生 | 5 | ~2% |
| D/E/F/G 未实现 | 272 | ~85% |

### 差距解读与优先级建议

R7RS-small 号称"small",但全库标识符仍有 **319** 个(base 239+
char/inexact/file/…等标准库)。当前实现的战略定位(DESIGN.md §6.2 自举面)
本就**不追求全量 R7RS**。真实差距集中在六块:

1. **数值塔补齐**(abs/floor/round/expt/quotient/gcd/lcm…≈38):
   多数可在 NPL 层以 define 定义(需闭包),少数(floor/exact 类)要
   runtime C 支持;
2. **list/vector 库**(map/for-each/append/reverse ≈30):
   NPL 层可写 —— 前提是自举编译器支持高阶函数捕获
   (collect_free_vars 已实现,g0/h2 用例已验证);
3. **派生形式**:nplc1 已接入 when/unless 与 case 子集;剩余 do、
   cond-expand 和完整 `case =>`;
4. **letrec 家族**:let* 与简单值绑定已可用;完整 letrec location
   预分配/相互递归语义仍需实现;
5. **异常族**(error/raise/guard/dynamic-wind):escape-only `call/cc` 与
   `call-with-current-continuation` 已完成；异常对象、handler 与 dynamic-wind 仍缺;
6. **模块系统**(define-library/syntax-error/include)与 bytevector/port:
   体量大、独立性强,建议排后。

### 落地优先级

C 剩余(do/cond-expand/`case =>`)→ E 完整 letrec 语义→
F-NPL 层(list/map 先行,依赖闭包)→ 数值塔 NPL 层 →
D(quasiquote;可先用 syntax-rules 简化版)→ 异常族(error/raise/guard)→
G(module;后续演进)。
