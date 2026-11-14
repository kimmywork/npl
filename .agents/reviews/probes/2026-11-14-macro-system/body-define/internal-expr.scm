(import (scheme base) (scheme write))
(define (f) (define-syntax two (syntax-rules () ((_) 2))) (two)) (write (f))
