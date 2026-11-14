(define-syntax swap! (syntax-rules () ((_ a b) (let ((tmp a)) (set! a b) (set! b tmp)))))
(define tmp 1) (define y 2) (swap! tmp y) (write (list tmp y))
