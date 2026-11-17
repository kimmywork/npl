(define-syntax cross (syntax-rules () ((_ (a ...) (b ...)) '((a b ...) ...))))
(write (cross (1 2) (x y)))
