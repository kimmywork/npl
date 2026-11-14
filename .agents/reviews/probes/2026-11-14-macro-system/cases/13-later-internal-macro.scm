(define (f) (define (g) (m)) (define-syntax m (syntax-rules () ((_) 42))) (g))
(write (f))
