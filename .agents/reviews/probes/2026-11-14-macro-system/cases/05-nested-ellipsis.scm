(define-syntax nest (syntax-rules () ((_ (a b ...) ...) '((a (b ...)) ...))))
(write (nest (1 2 3) (4) (5 6)))
