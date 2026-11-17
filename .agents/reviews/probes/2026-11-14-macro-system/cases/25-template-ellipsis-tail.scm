(define-syntax tt (syntax-rules () ((_ (a ...) ...) '(a ... ...))))
(write (tt (1 2) (3)))
