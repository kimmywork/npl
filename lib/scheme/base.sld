(define-library (scheme base)
  (export
    (rename integer-add +) (rename integer-subtract -)
    (rename integer-multiply *) (rename integer-less <)
    (rename integer-less-equal <=) (rename integer-equal =)
    (rename integer-greater >) (rename integer-greater-equal >=)
    append bytevector bytevector-length bytevector-u8-ref
    bytevector-u8-set! bytevector? binary-port?
    car cdr char->integer char?
    close-input-port close-output-port close-port cons
    call-with-port
    current-error-port current-input-port current-output-port
    dynamic-wind eof-object eof-object? eq? eqv?
    flush-output-port get-output-bytevector get-output-string
    input-port-open? input-port? integer->char list list->vector length
    newline not null? number->string number?
    open-input-bytevector open-input-string
    open-output-bytevector open-output-string
    output-port-open? output-port? pair? peek-char peek-u8 port?
    raise raise-continuable reverse list?
    read-bytevector read-bytevector! read-char read-u8
    set-car! set-cdr! string-append string-length string-ref string=?
    substring symbol? symbol->string textual-port?
    values vector vector-length vector-ref vector-set! vector?
    call-with-current-continuation call/cc call-with-values
    with-exception-handler write-bytevector write-char write-u8
    and or when unless cond case do let*-values guard
    make-parameter parameterize make-promise promise? force delay
    delay-force)
  (import (rename (npl primitive)
            (+ raw-add) (- raw-subtract) (* raw-multiply)
            (= raw-equal) (< raw-less) (<= raw-less-equal)
            (> raw-greater) (>= raw-greater-equal)
            (number->string raw-number->string)))
  (include "integer.scm")
  (begin
    (define (call-with-port port proc)
      (call-with-values
        (lambda () (proc port))
        (lambda values
          (close-port port)
          (npl_list_to_values values))))
    (define (list . values) values)
    (define (length-loop values count)
      (if (null? values)
          count
          (length-loop (cdr values) (raw-add count 1))))
    (define (length values) (length-loop values 0))
    (define (reverse-loop values output)
      (if (null? values)
          output
          (reverse-loop (cdr values) (cons (car values) output))))
    (define (reverse values) (reverse-loop values '()))
    (define (list? value)
      (if (null? value)
          #t
          (if (pair? value) (list? (cdr value)) #f)))))
