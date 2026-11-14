(define-syntax lit (syntax-rules () ((_ 1) 'one) ((_ "s") 'str) ((_ #\c) 'chr) ((_ x) 'other)))
(write (list (lit 1) (lit "s") (lit #\c) (lit 2)))
