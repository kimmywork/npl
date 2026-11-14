(define-syntax q (syntax-rules () ((_) '(a b))))
(define-syntax qs (syntax-rules () ((_) 'tmp)))
(write (list (q) (eq? (qs) 'tmp) (symbol->string (qs))))
