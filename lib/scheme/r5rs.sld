; R7RS-small (scheme r5rs): the R5RS names, re-exported from the R7RS
; libraries that define them (R7RS Appendix A). Core syntax (define, lambda,
; if, quote, let, ...) is built into every NPL program and is not an export.
(define-library (scheme r5rs)
  (export
    * + - / < <= = > >= abs and append apply assoc assq assv boolean? caar
    cadr call-with-current-continuation call-with-values car case cdar cddr
    cdr ceiling char->integer char-ready? char<=? char<? char=? char>=? char>?
    char? close-input-port close-output-port complex? cond cons
    current-input-port current-output-port delay denominator do dynamic-wind
    eof-object? eq? equal? eqv? even? exact->inexact exact? expt floor
    for-each force gcd imag-part inexact->exact inexact? input-port?
    integer->char integer? lcm length list list->string list->vector list-ref
    list-tail list? make-rectangular make-string make-vector map max member
    memq memv min modulo negative? newline not null? number->string number?
    numerator odd? or output-port? pair? peek-char positive? procedure?
    quotient rational? rationalize read-char real-part real? remainder reverse
    round set-car! set-cdr! string string->list string->number string->symbol
    string-append string-copy string-fill! string-length string-ref
    string-set! string<=? string<? string=? string>=? string>? string?
    substring symbol->string symbol? truncate values vector vector->list
    vector-fill! vector-length vector-ref vector-set! vector? write-char zero?
    char-alphabetic? char-ci<? char-ci=? char-ci>=? char-ci>? char-downcase
    char-lower-case? char-numeric? char-upcase char-upper-case?
    char-whitespace? string-ci<=? string-ci<? string-ci=? string-ci>=?
    string-ci>? angle magnitude make-polar caaaar caaadr caaar caadar caaddr
    caadr cadaar cadadr cadar caddar cadddr caddr cdaaar cdaadr cdaar cdadar
    cdaddr cdadr cddaar cddadr cddar cdddar cddddr cdddr eval
    call-with-input-file call-with-output-file open-input-file
    open-output-file with-input-from-file with-output-to-file acos asin atan
    cos exp log sin sqrt tan load read interaction-environment display write
    null-environment scheme-report-environment)
  (import
    (only (scheme base)
          error string-append * + - / < <= = > >= abs and append apply assoc
          assq assv boolean? caar cadr call-with-current-continuation
          call-with-values car case cdar cddr cdr ceiling char->integer
          char-ready? char<=? char<? char=? char>=? char>? char?
          close-input-port close-output-port complex? cond cons
          current-input-port current-output-port delay denominator do
          dynamic-wind eof-object? eq? equal? eqv? even? exact->inexact exact?
          expt floor for-each force gcd imag-part inexact->exact inexact?
          input-port? integer->char integer? lcm length list list->string
          list->vector list-ref list-tail list? make-rectangular make-string
          make-vector map max member memq memv min modulo negative? newline
          not null? number->string number? numerator odd? or output-port?
          pair? peek-char positive? procedure? quotient rational? rationalize
          read-char real-part real? remainder reverse round set-car! set-cdr!
          string string->list string->number string->symbol string-append
          string-copy string-fill! string-length string-ref string-set!
          string<=? string<? string=? string>=? string>? string? substring
          symbol->string symbol? truncate values vector vector->list
          vector-fill! vector-length vector-ref vector-set! vector? write-char
          zero?)
    (only (scheme char)
          char-alphabetic? char-ci<? char-ci=? char-ci>=? char-ci>?
          char-downcase char-lower-case? char-numeric? char-upcase
          char-upper-case? char-whitespace? string-ci<=? string-ci<?
          string-ci=? string-ci>=? string-ci>?)
    (only (scheme complex)
          angle magnitude make-polar)
    (only (scheme cxr)
          caaaar caaadr caaar caadar caaddr caadr cadaar cadadr cadar caddar
          cadddr caddr cdaaar cdaadr cdaar cdadar cdaddr cdadr cddaar cddadr
          cddar cdddar cddddr cdddr)
    (only (scheme eval)
          eval environment)
    (only (scheme file)
          call-with-input-file call-with-output-file open-input-file
          open-output-file with-input-from-file with-output-to-file)
    (only (scheme inexact)
          acos asin atan cos exp log sin sqrt tan)
    (only (scheme load)
          load)
    (only (scheme read)
          read)
    (only (scheme repl)
          interaction-environment)
    (only (scheme write)
          display write))
  (begin
    ; R7RS 6.12: environments for the R5RS bindings; only version 5 is
    ; supported. The null environment holds the syntactic keywords: core
    ; syntax plus the derived forms that (scheme base) exports.
    (define (check-version who version)
      (if (not (eqv? version 5))
          (error (string-append who ": unsupported version") version)))
    (define (scheme-report-environment version)
      (check-version "scheme-report-environment" version)
      (environment '(scheme r5rs)))
    (define (null-environment version)
      (check-version "null-environment" version)
      (environment '(only (scheme base) and case cond delay do or)))))
