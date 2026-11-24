(define-syntax count-down (syntax-rules () ((_) 0) ((_ x . rest) (+ 1 (count-down . rest)))))
(write (count-down a b c d e f g h i j a b c d e f g h i j a b c d e f g h i j a b c d e f g h i j))
