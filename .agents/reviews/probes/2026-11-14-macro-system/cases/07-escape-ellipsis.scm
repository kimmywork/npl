(define-syntax def-lister
  (syntax-rules () ((_ name) (define-syntax name (syntax-rules () ((_ x (... ...)) (list x (... ...))))))))
(def-lister my-list)
(write (my-list 1 2 3))
