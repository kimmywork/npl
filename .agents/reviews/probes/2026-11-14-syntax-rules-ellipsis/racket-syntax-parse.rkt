#lang racket/base
(require racket/function (for-syntax racket/base syntax/parse))
;; 1. 内置：curried define、curry
(define ((adder n) m) (+ n m))
(displayln (list ((adder 1) 2) ((curry + 1) 2) (((curry (lambda (x y z) (list x y z)) 1) 2) 3)))
;; 2. syntax-parse 版的 \ ：同层多个省略号、语法类、~not 都可用
(define-syntax (fn stx)
  (syntax-parse stx
    #:datum-literals (=>)
    [(_ (~and x:id (~not =>)) ... => body ...+ => more ...+)
     #'(lambda (x ...) (fn body ... => more ...))]
    [(_ (~and x:id (~not =>)) ... => body ...+)
     #'(lambda (x ...) body ...)]
    [(_ (x:id ... . rest) => body ...+)
     #'(lambda (x ... . rest) body ...)]))
(displayln (list ((fn x y => (+ x y)) 1 2)
                 (((fn x => y => (+ x y)) 1) 2)
                 ((fn (x . r) => (list x r)) 1 2 3)))
