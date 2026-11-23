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
;; Literals in spliced output stay literals; define-values from a macro, and
;; its initializer, are expanded once; a template refers to the innermost
;; binding at the macro definition site; () matches a (p ...) subpattern.
(define-syntax define-literals
  (syntax-rules ()
    ((_ s c v b) (begin (define s "x") (define c #\y) (define v #(1 "z"))
                        (define b #t)))))
(define-literals lit-s lit-c lit-v lit-t)
(define-syntax define-both
  (syntax-rules ()
    ((_ a b e) (define-values (a b) e))))
(define (values-results)
  (define-both a b (values (list 3) (list 4)))
  (define-values (c . d) (values (list 5) 6 7))
  (list a b c d))
(define (nearest-binding)
  (let ((x 'outer))
    (let ((x 'inner))
      (let-syntax ((m (syntax-rules () ((_) x))))
        (m)))))
(define-syntax count-bindings
  (syntax-rules ()
    ((_ ((name value) ...) body) (let ((name value) ...) body))))
(define (more-results)
  (report 'literals (list lit-s lit-c lit-v lit-t))
  (report 'define-values (values-results))
  (report 'nearest-binding (nearest-binding))
  (report 'empty-subpattern (list (count-bindings () 'none)
                                  (count-bindings ((a 1) (b 2)) (+ a b)))))
(define fixture-result (begin (definition-results) (more-results) 0))
