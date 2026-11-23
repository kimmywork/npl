; NPL syntax-case extensions and phase-1 forms.  Guile has no oracle for
; begin-for-syntax/for-syntax, so the native and REPL surfaces are compared
; with the behavior fixed by this file.
(define (report name value) (write (list name value)) (newline))

; Syntax-case patterns use the same tail-pattern support as syntax-rules.
(define-syntax tail-pattern
  (lambda (stx)
    (syntax-case stx ()
      [(_ (a b . rest)) (syntax (list 'pair a b 'tail (quote rest)))])))

(report 'tail-pattern (tail-pattern (1 2 3 4)))

; syntax-e and syntax->list are NPL/Racket helpers beyond R6RS.
(define-syntax first-datum
  (lambda (stx)
    (syntax-case stx ()
      [(_ x) (syntax-e (syntax x))])))

(define-syntax quoted-datum
  (lambda (stx)
    (syntax-case stx ()
      [(_ x) (syntax (quote x))])))

(report 'syntax-e (first-datum 5))
(report 'syntax-e-list (quoted-datum (a b)))

(define-syntax list-length
  (lambda (stx)
    (syntax-case stx ()
      [(_ item ...)
       (length (syntax->list (syntax (item ...))))])))

(report 'syntax-list (list-length a b c))

; Phase-1 values and functions.
(begin-for-syntax
  (define phase-value 40)
  (define (phase-plus x) (+ x 2)))

(for-syntax
  (define for-syntax-value 42))

(define-syntax phase-result
  (lambda (stx)
    (syntax-case stx ()
      [(_) (phase-plus phase-value)])))

(define-syntax for-syntax-result
  (lambda (stx)
    (syntax-case stx ()
      [(_) for-syntax-value])))

(report 'phase-value (phase-result))
(report 'for-syntax-value (for-syntax-result))

; A syntax-rules macro defined at phase 1 is expanded in transformer code.
(begin-for-syntax
  (define-syntax phase-double
    (syntax-rules ()
      ((_ x) (* 2 x)))))

(define-syntax phase-macro-result
  (lambda (stx)
    (syntax-case stx ()
      [(_) (phase-double 21)])))

(report 'phase-macro (phase-macro-result))

; letrec-syntax inside a transformer body.
(define-syntax count-items
  (lambda (stx)
    (letrec-syntax ((count
                      (syntax-rules ()
                        ((_) 0)
                        ((_ x rest ...) (+ 1 (count rest ...))))))
      (syntax-case stx ()
        [(_ a b c) (count a b c)]))))

(report 'letrec-syntax (count-items a b c))

; Definition-producing procedural output.
(define-syntax define-both
  (lambda (stx)
    (syntax-case stx ()
      [(_ a b) (syntax (define-values (a b) (values 1 2)))])))

(define-both dv-a dv-b)
(report 'define-values (list dv-a dv-b))
