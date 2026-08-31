(define-library (scheme base)
  (export
    + - * < <= = > >=
    append bytevector bytevector-length bytevector-u8-ref
    bytevector-u8-set! bytevector? binary-port?
    car cdr char->integer char?
    close-input-port close-output-port close-port cons
    current-error-port current-input-port current-output-port
    dynamic-wind eof-object eof-object? eq? eqv?
    flush-output-port get-output-bytevector
    input-port-open? input-port? integer->char list->vector
    newline not null? number->string number?
    open-input-bytevector open-output-bytevector
    output-port-open? output-port? pair? peek-char peek-u8 port?
    raise raise-continuable
    read-bytevector read-bytevector! read-char read-u8
    set-car! set-cdr! string-append string-length string-ref string=?
    substring symbol->string textual-port?
    values vector vector-length vector-ref vector-set! vector?
    call-with-current-continuation call/cc call-with-values
    with-exception-handler write-bytevector write-char write-u8
    and or when unless cond case do let*-values guard
    make-parameter parameterize make-promise promise? force delay
    delay-force)
  (import (npl primitive)))
