(define-library (scheme base)
  (export
    (rename integer-add +) (rename integer-subtract -)
    (rename integer-multiply *) (rename rational-divide /) (rename integer-less <)
    (rename integer-less-equal <=) (rename integer-equal =)
    (rename integer-greater >) (rename integer-greater-equal >=)
    (rename integer-gcd gcd) (rename integer-lcm lcm)
    (rename integer-quotient quotient) (rename integer-remainder remainder)
    (rename integer-modulo modulo)
    bytevector bytevector-length bytevector-u8-ref
    bytevector-u8-set! bytevector? binary-port?
    car cdr char->integer char?
    close-input-port close-output-port close-port cons
    call-with-port
    current-error-port current-input-port current-output-port
    dynamic-wind eof-object eof-object? eq? eqv?
    flush-output-port get-output-bytevector get-output-string
    input-port-open? input-port? integer->char list list->vector length
    newline not null? number->string number? string->number
    rational? real? complex? integer? exact-integer? exact? inexact?
    make-rectangular real-part imag-part
    exact inexact exact->inexact inexact->exact
    numerator denominator abs positive? negative? zero? even? odd?
    expt square floor ceiling truncate round max min rationalize
    floor/ floor-quotient floor-remainder
    truncate/ truncate-quotient truncate-remainder exact-integer-sqrt
    open-input-bytevector open-input-string
    open-output-bytevector open-output-string
    output-port-open? output-port? pair? peek-char peek-u8 port?
    raise raise-continuable reverse list?
    read-bytevector read-bytevector! read-char read-u8
    set-car! set-cdr! string? string-append string-length string-ref
    substring symbol? symbol->string string->symbol textual-port?
    values vector vector-length vector-ref vector-set! vector? apply
    call-with-current-continuation call/cc call-with-values
    with-exception-handler write-bytevector write-char write-u8
    and or when unless cond case do let*-values guard
    make-parameter parameterize make-promise promise? force delay
    delay-force
    caar cadr cdar cddr boolean? boolean=? symbol=? procedure?
    map for-each make-list list-tail list-ref list-set! list-copy
    memq memv member assq assv assoc equal? append
    make-vector vector->list vector-copy vector-copy! vector-fill!
    vector-append vector-map vector-for-each
    make-bytevector bytevector-copy bytevector-copy! bytevector-append
    char=? char<? char>? char<=? char>=?
    make-string string list->string string->list string-copy string-copy!
    string-fill! string-set! vector->string string->vector string->utf8
    utf8->string string=? string<? string>? string<=? string>=?
    string-map string-for-each define-record-type
    error error-object? error-object-message error-object-irritants
    file-error? read-error?
    read-line read-string write-string char-ready? u8-ready? features)
  (import (rename (except (npl primitive)
                    current-input-port current-output-port current-error-port)
            (+ raw-add) (- raw-subtract) (* raw-multiply)
            (= raw-equal) (< raw-less) (<= raw-less-equal)
            (> raw-greater) (>= raw-greater-equal)
            (npl_number_gcd raw-gcd) (npl_number_quotient raw-quotient)
            (npl_number_remainder raw-remainder)
            (number->string raw-number->string) (append raw-append)
            (string=? raw-string=?)))
  ; Core list procedures first: record.scm and error.scm run code at load.
  (begin
    (define (call-with-port port proc)
      (call-with-values
        (lambda () (proc port))
        (lambda values
          (close-port port)
          (npl_list_to_values values))))
    (define (list . values) values)
    ; Current ports are parameter objects (R7RS 6.13.1), so parameterize
    ; rebinds them; calling one returns the port.
    (define current-input-port (npl_port_parameter 0))
    (define current-output-port (npl_port_parameter 1))
    (define current-error-port (npl_port_parameter 2))
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
          (if (pair? value) (list? (cdr value)) #f))))
  (include "integer.scm" "rational.scm" "complex.scm" "real.scm" "list.scm"
           "vector.scm" "string.scm" "record.scm"
           "error.scm" "text-io.scm"))
