(define-syntax only-ids (syntax-rules () ((_ x) (syntax-error "only-ids: bad form" x))))
(write (only-ids 1))
