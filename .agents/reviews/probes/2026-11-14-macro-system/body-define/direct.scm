(import (scheme base) (scheme write))
(write (let () (define-syntax def (syntax-rules () ((_ n) (define (n) 'v)))) (def get) (get)))
