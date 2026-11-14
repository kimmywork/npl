(define-syntax def-point (syntax-rules () ((_ make get) (define-record-type point (make x) p? (x get)))))
(write (let () (def-point mk px) (px (mk 7))))
