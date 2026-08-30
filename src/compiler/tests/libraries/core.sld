(define-library (libraries core)
  (export base-value hidden old-name add)
  (import (scheme base))
  (begin
    (define base-value 20)
    (define hidden 10)
    (define old-name 12)
    (define (add left right) (+ left right))))
