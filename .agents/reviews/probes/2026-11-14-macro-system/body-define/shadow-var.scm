(import (scheme base) (scheme write))
(define-syntax def (syntax-rules () ((_ n) (define n 'inner)))) (define x 'outer) (define (f) (def x) x) (write (list (f) x))
