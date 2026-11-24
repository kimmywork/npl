(import (scheme base) (scheme write))
(define-syntax def (syntax-rules () ((_ n) (define (n) 'inner)))) (define (get) 'outer) (define (f) (def get) (get)) (write (list (f) (get)))
