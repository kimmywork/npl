(define-library (libraries cycle-b)
  (export value)
  (import (libraries cycle-a))
  (begin (define value 2)))
