; Library-declared C primitives (frontend/c-primitives.npl): runtime C
; functions that are not PRIMITIVE-TABLE rows, declared without a compiler
; change.
(define-library (libraries c-primitives)
  (export decimal? decimal-count (rename decimal? integer-text?)
          number-text)
  (import (scheme base))
  (begin
    ; Symbol by rule would be npl_decimal_p; name it explicitly.
    (define-c-primitive (decimal? (text string)) boolean
      "npl_integer_string_p")
    ; Symbol by rule: npl_number_to_string_radix.
    (define-c-primitive (number->string-radix (n number) (radix fixnum))
      string)
    (define (number-text n) (number->string-radix n 16))
    (define (decimal-count texts)
      (let loop ((texts texts) (count 0))
        (if (null? texts)
            count
            (loop (cdr texts) (if (decimal? (car texts)) (+ count 1) count)))))))
