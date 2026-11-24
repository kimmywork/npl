(define-syntax vp (syntax-rules () ((_ #(a ... b)) '((a ...) b))))
(write (vp #(1 2 3)))
