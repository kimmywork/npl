(define-syntax map-call (syntax-rules () ((_ f (x ...)) (list (f x) ...))))
(write (map-call - (1 2 3)))
