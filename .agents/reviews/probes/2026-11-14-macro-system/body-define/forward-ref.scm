(import (scheme base) (scheme write))
(define-syntax def (syntax-rules () ((_ n) (define n 'inner)))) (define (f) (define (g) x) (def x) (g)) (write (f))
