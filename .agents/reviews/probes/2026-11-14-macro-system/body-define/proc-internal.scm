(import (scheme base) (scheme write))
(define (f) (define-syntax def (syntax-rules () ((_ n) (define (n) 'v)))) (def get) (get)) (write (f))
