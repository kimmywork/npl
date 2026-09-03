# R7RS-small Binding Inventory Snapshot

状态：基线审计快照；逐项语义 conformance 尚未关闭。
日期：2026-09-03

## 状态说明

| 状态 | 含义 |
|---|---|
| `existing-tested` | 已有实现并有直接项目测试；不重做 |
| `existing-partial` | 已有实现但有明确缺口 |
| `surface-present` | 导出/源码存在，但尚无逐项标准行为证据 |
| `missing` | 标准导出当前不存在 |
| `wrong-surface` | 当前导出位置或公共 surface 与 R7RS 不一致 |

## 逐项清单

| Library | Identifier | R7RS surface | Current surface | Status | Evidence / gap |
|---|---|---|---|---|---|
| `(scheme base)` | `*` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `+` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `-` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `...` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `/` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `<` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `<=` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `=` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `=>` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `>` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `>=` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `_` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `abs` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `and` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `append` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `apply` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `assoc` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `assq` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `assv` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `begin` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `binary-port?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `boolean=?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `boolean?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `bytevector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `bytevector-append` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `bytevector-copy` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `bytevector-copy!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `bytevector-length` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `bytevector-u8-ref` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `bytevector-u8-set!` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `bytevector?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `caar` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `cadr` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `call-with-current-continuation` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `call-with-port` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `call-with-values` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `call/cc` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `car` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `case` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `cdar` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `cddr` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `cdr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `ceiling` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `char->integer` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `char-ready?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `char<=?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `char<?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `char=?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `char>=?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `char>?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `char?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `close-input-port` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `close-output-port` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `close-port` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `complex?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `cond` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `cond-expand` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `cons` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `current-error-port` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `current-input-port` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `current-output-port` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `define` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `define-record-type` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `define-syntax` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `define-values` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `denominator` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `do` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `dynamic-wind` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `else` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `eof-object` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `eof-object?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `eq?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `equal?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `eqv?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `error` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `error-object-irritants` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `error-object-message` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `error-object?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `even?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `exact` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `exact-integer-sqrt` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `exact-integer?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `exact?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `expt` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `features` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `file-error?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `floor` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `floor-quotient` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `floor-remainder` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `floor/` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `flush-output-port` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `for-each` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `gcd` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `get-output-bytevector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `get-output-string` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `guard` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `if` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `include` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `include-ci` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `inexact` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `inexact?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `input-port-open?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `input-port?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `integer->char` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `integer?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `lambda` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `lcm` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `length` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `let` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `let*` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `let*-values` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `let-syntax` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `let-values` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `letrec` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `letrec*` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `letrec-syntax` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `list` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `list->string` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `list->vector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `list-copy` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `list-ref` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `list-set!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `list-tail` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `list?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `make-bytevector` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `make-list` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `make-parameter` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `make-string` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `make-vector` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `map` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `max` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `member` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `memq` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `memv` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `min` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `modulo` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `negative?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `newline` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `not` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `null?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `number->string` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `number?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `numerator` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `odd?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `open-input-bytevector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `open-input-string` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `open-output-bytevector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `open-output-string` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `or` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `output-port-open?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `output-port?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `pair?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `parameterize` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `peek-char` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `peek-u8` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `port?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `positive?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `procedure?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `quasiquote` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `quote` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `quotient` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `raise` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `raise-continuable` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `rational?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `rationalize` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `read-bytevector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `read-bytevector!` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `read-char` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `read-error?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `read-line` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `read-string` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `read-u8` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `real?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `remainder` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `reverse` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `round` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `set!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `set-car!` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `set-cdr!` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `square` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string->list` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string->number` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string->symbol` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string->utf8` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string->vector` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string-append` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `string-copy` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string-copy!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string-fill!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string-for-each` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string-length` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `string-map` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string-ref` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `string-set!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string<=?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string<?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string=?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `string>=?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string>?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `string?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `substring` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `symbol->string` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `symbol=?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `symbol?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `syntax-error` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `syntax-rules` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `textual-port?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `truncate` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `truncate-quotient` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `truncate-remainder` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `truncate/` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `u8-ready?` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `unless` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `unquote` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `unquote-splicing` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `utf8->string` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `values` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `vector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `vector->list` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector->string` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector-append` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector-copy` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector-copy!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector-fill!` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector-for-each` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector-length` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `vector-map` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `vector-ref` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `vector-set!` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `vector?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `when` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `with-exception-handler` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `write-bytevector` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `write-char` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `write-string` | yes | no | `missing` | not exported by current library |
| `(scheme base)` | `write-u8` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme base)` | `zero?` | yes | no | `missing` | not exported by current library |
| `(case lambda)` | `case-lambda` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-alphabetic?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-ci<=?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-ci<?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-ci=?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-ci>=?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-ci>?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-downcase` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-foldcase` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-lower-case?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-numeric?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-upcase` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-upper-case?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `char-whitespace?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `digit-value` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme char)` | `string-ci<=?` | yes | no | `missing` | not exported by current library |
| `(scheme char)` | `string-ci<?` | yes | no | `missing` | not exported by current library |
| `(scheme char)` | `string-ci=?` | yes | no | `missing` | not exported by current library |
| `(scheme char)` | `string-ci>=?` | yes | no | `missing` | not exported by current library |
| `(scheme char)` | `string-ci>?` | yes | no | `missing` | not exported by current library |
| `(scheme char)` | `string-downcase` | yes | no | `missing` | not exported by current library |
| `(scheme char)` | `string-foldcase` | yes | no | `missing` | not exported by current library |
| `(scheme char)` | `string-upcase` | yes | no | `missing` | not exported by current library |
| `(scheme complex)` | `angle` | yes | no | `missing` | not exported by current library |
| `(scheme complex)` | `imag-part` | yes | no | `missing` | not exported by current library |
| `(scheme complex)` | `magnitude` | yes | no | `missing` | not exported by current library |
| `(scheme complex)` | `make-polar` | yes | no | `missing` | not exported by current library |
| `(scheme complex)` | `make-rectangular` | yes | no | `missing` | not exported by current library |
| `(scheme complex)` | `real-part` | yes | no | `missing` | not exported by current library |
| `(scheme cxr)` | `caaaar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `caaadr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `caaar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `caadar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `caaddr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `caadr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cadaar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cadadr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cadar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `caddar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cadddr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `caddr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdaaar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdaadr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdaar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdadar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdaddr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdadr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cddaar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cddadr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cddar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdddar` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cddddr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme cxr)` | `cdddr` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme eval)` | `environment` | yes | yes | `existing-partial` | known crossing/syntax gaps |
| `(scheme eval)` | `eval` | yes | yes | `existing-partial` | known crossing/syntax gaps |
| `(scheme file)` | `call-with-input-file` | yes | no | `missing` | not exported by current library |
| `(scheme file)` | `call-with-output-file` | yes | no | `missing` | not exported by current library |
| `(scheme file)` | `delete-file` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme file)` | `file-exists?` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme file)` | `open-binary-input-file` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme file)` | `open-binary-output-file` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme file)` | `open-input-file` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme file)` | `open-output-file` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme file)` | `with-input-from-file` | yes | no | `missing` | not exported by current library |
| `(scheme file)` | `with-output-to-file` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `acos` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `asin` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `atan` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `cos` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `exp` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `finite?` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `infinite?` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `log` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `nan?` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `sin` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `sqrt` | yes | no | `missing` | not exported by current library |
| `(scheme inexact)` | `tan` | yes | no | `missing` | not exported by current library |
| `(scheme lazy)` | `delay` | yes | yes | `existing-tested` | existing promise implementation and tests |
| `(scheme lazy)` | `delay-force` | yes | yes | `existing-tested` | existing promise implementation and tests |
| `(scheme lazy)` | `force` | yes | yes | `existing-tested` | existing promise implementation and tests |
| `(scheme lazy)` | `make-promise` | yes | yes | `existing-tested` | existing promise implementation and tests |
| `(scheme lazy)` | `promise?` | yes | yes | `existing-tested` | existing promise implementation and tests |
| `(scheme load)` | `load` | yes | yes | `existing-partial` | single-argument subset; second environment specifier incomplete |
| `(process context)` | `command-line` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(process context)` | `emergency-exit` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(process context)` | `exit` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(process context)` | `get-environment-variable` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(process context)` | `get-environment-variables` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme read)` | `read` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme repl)` | `interaction-environment` | yes | yes | `existing-partial` | source export exists; needs focused conformance |
| `(scheme time)` | `current-jiffy` | yes | no | `missing` | not exported by current library |
| `(scheme time)` | `current-second` | yes | no | `missing` | not exported by current library |
| `(scheme time)` | `jiffies-per-second` | yes | no | `missing` | not exported by current library |
| `(scheme write)` | `display` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme write)` | `write` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme write)` | `write-shared` | yes | yes | `surface-present` | source export exists; needs focused conformance |
| `(scheme write)` | `write-simple` | yes | yes | `surface-present` | source export exists; needs focused conformance |

## Current exports absent from the corresponding R7RS library

| Library | Extra current export | Classification |
|---|---|---|
| `(scheme base)` | `delay` | `wrong-surface` |
| `(scheme base)` | `delay-force` | `wrong-surface` |
| `(scheme base)` | `force` | `wrong-surface` |
| `(scheme base)` | `make-promise` | `wrong-surface` |
| `(scheme base)` | `promise?` | `wrong-surface` |
| `(scheme char)` | `char<=?` | `wrong-surface` |
| `(scheme char)` | `char<?` | `wrong-surface` |
| `(scheme char)` | `char=?` | `wrong-surface` |
| `(scheme char)` | `char>=?` | `wrong-surface` |
| `(scheme char)` | `char>?` | `wrong-surface` |

## Summary

| Library | R7RS exports | Current exports | Missing | Extra |
|---|---:|---:|---:|---:|
| `(scheme base)` | 238 | 102 | 141 | 5 |
| `(case lambda)` | 1 | 1 | 0 | 0 |
| `(scheme char)` | 22 | 19 | 8 | 5 |
| `(scheme complex)` | 6 | 0 | 6 | 0 |
| `(scheme cxr)` | 24 | 24 | 0 | 0 |
| `(scheme eval)` | 2 | 2 | 0 | 0 |
| `(scheme file)` | 10 | 6 | 4 | 0 |
| `(scheme inexact)` | 12 | 0 | 12 | 0 |
| `(scheme lazy)` | 5 | 5 | 0 | 0 |
| `(scheme load)` | 1 | 1 | 0 | 0 |
| `(process context)` | 5 | 5 | 0 | 0 |
| `(scheme r5rs)` | 221 | 0 | 221 | 0 |
| `(scheme read)` | 1 | 1 | 0 | 0 |
| `(scheme repl)` | 1 | 1 | 0 | 0 |
| `(scheme time)` | 3 | 0 | 3 | 0 |
| `(scheme write)` | 4 | 4 | 0 | 0 |

## Evidence paths

- Specification: `docs/reference/r7rs.html`.
- Current public surfaces: `lib/scheme/*.sld`.
- Existing compiler tests: `src/compiler/tests/` and `tools/check_full_compiler.sh`.
- Existing REPL tests: `src/repl/tests/` and `tools/check_repl.sh`.
- External oracle available: Guile 3.0.11.

This table is a surface/status baseline. A `surface-present` row is not a claim of complete semantics; it is an instruction to add the focused behavior evidence in the corresponding implementation increment.
