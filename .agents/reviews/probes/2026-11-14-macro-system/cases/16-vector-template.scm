(define-syntax vt (syntax-rules () ((_ x ...) '#(x ... end))))
(write (vt 1 2))
