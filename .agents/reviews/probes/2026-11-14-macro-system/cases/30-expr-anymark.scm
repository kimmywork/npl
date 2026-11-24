(define-syntax make-getter
  (syntax-rules ()
    ((_ getter body ...)
     (let ((secret 'inner))
       (let-syntax ((getter (syntax-rules () ((_) secret))))
         body ...)))))
(define secret 'outer)
(write (make-getter g (g)))
