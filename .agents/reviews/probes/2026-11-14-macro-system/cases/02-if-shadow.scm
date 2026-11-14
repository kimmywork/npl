(define-syntax my-unless (syntax-rules () ((_ c e) (if c #f e))))
(write (let ((if list)) (my-unless #f 'ok)))
