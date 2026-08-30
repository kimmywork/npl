(define-library (libraries syntax)
  (export increment definition-site-value)
  (import (scheme base))
  (begin
    (define definition-site-value 40)
    (define-syntax increment
      (syntax-rules ()
        ((_ value) (+ definition-site-value value))))))
