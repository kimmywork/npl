(define-syntax my-or (syntax-rules () ((_) #f) ((_ e) e) ((_ e r ...) (let ((t e)) (if t t (my-or r ...))))))
(define t 5)
(write (list (my-or #f t) (let ((my-or (lambda x 'proc))) (my-or 1 2))))
