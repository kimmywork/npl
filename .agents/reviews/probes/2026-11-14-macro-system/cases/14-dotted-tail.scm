(define-syntax dt (syntax-rules () ((_ a . b) '(a b))))
(define-syntax et (syntax-rules () ((_ a ... . r) '((a ...) r))))
(write (list (dt 1 2 3) (et 1 2 . 3)))
