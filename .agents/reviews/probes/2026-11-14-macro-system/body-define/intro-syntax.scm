(import (scheme base) (scheme write))
(define-syntax mk (syntax-rules () ((_ d body ...) (let () (define-syntax d (syntax-rules () ((_ n) (define (n) 'v)))) body ...)))) (write (mk def (def get) (get)))
