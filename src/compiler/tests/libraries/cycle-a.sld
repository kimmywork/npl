(define-library (libraries cycle-a)
  (export value)
  (import (libraries cycle-b))
  (begin (define value 1)))
