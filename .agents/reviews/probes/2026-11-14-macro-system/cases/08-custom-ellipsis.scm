(define-syntax my-list (syntax-rules ::: () ((_ x :::) (list x :::))))
(write (my-list 1 2 3))
