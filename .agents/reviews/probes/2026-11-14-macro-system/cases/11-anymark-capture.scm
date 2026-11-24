(define tmp 'outer)
(define-syntax with-tmp (syntax-rules () ((_ body) (let ((tmp 'inner)) body))))
(write (with-tmp (let-syntax ((m (syntax-rules () ((_) tmp)))) (m))))
