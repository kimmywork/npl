(define-library (libraries repl-cycle-a)
  (export value)
  (import (libraries repl-cycle-b))
  (begin (define value 1)))
