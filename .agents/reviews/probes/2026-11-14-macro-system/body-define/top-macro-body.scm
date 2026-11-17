(import (scheme base) (scheme write))
(define-syntax def (syntax-rules () ((_ n) (define (n) 'v)))) (define (f) (def get) (get)) (write (f))
