#lang racket/base
(require (for-syntax racket/base syntax/parse))
(define-syntax (fn stx)
  (syntax-parse stx #:datum-literals (=>)
    [(_ (~and x:id (~not =>)) ... => body ...+) #'(lambda (x ...) body ...)]))
(fn (x . r) => x)
