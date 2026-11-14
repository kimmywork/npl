(import (scheme base) (scheme write))
(define-syntax mk (syntax-rules () ((_ d) (define-syntax d (syntax-rules () ((_ n) (define (n) 'v))))))) (mk def) (def get) (write (get))
