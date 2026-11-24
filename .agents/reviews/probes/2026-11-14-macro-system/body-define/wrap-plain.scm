(import (scheme base) (scheme write))
(define-syntax wrap (syntax-rules () ((_ body ...) (let () body ...)))) (write (wrap (define (get) 'v) (get)))
