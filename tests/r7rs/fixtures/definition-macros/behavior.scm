; Macros that expand to definitions (R7RS 5.3, 4.3): the output is spliced
; into the enclosing top level or body, and identifiers the template
; introduces refer to the bindings visible where the macro was defined.
(define (report name value) (write (list name value)) (newline))
(define (helper x) (* x 100))
(define-syntax define-pair
  (syntax-rules ()
    ((_ a b v) (begin (define a (+ v 1)) (define b (list a v (helper v)))))))
(define-pair top-a top-b 10)
(define-syntax define-counter
  (syntax-rules ()
    ((_ name) (begin (define count 0)
                     (define (name) (set! count (+ count 1)) count)))))
(define-syntax define-getter
  (syntax-rules ()
    ((_ name value) (define (name) (list 'quoted value)))))
(define-getter get-five 5)
(define (body-results)
  (define-pair inner-a inner-b 1)
  (define-counter tick)
  (tick)
  (list inner-a inner-b (tick)))
(define (definition-results)
  (report 'top (list top-a top-b))
  (report 'body (body-results))
  (report 'quoted (get-five))
  (report 'shadow (let ((helper (lambda (x) 'shadowed)))
                    (define-pair p q 2)
                    q)))
(define fixture-result (begin (definition-results) 0))
