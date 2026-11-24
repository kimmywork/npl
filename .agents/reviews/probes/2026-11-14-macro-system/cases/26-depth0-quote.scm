(define-syntax pairs (syntax-rules () ((_ a (b ...)) '((a b) ...))))
(write (pairs k (1 2 3)))
