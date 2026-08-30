(define-library (libraries extra)
  (export kept omitted)
  (import (scheme base))
  (begin
    (define kept 42)
    (define omitted 100)))
