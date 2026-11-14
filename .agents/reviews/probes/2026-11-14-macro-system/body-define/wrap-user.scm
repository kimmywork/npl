(import (scheme base) (scheme write))
(define-syntax wrap (syntax-rules () ((_ body ...) (let () body ...)))) (write (wrap (define-syntax def (syntax-rules () ((_ n) (define (n) 'v)))) (def get) (get)))
