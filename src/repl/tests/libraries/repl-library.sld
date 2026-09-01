(define-library (libraries repl-library)
  (export cached-value included-value plus-forty hidden)
  (import (scheme base))
  (include "repl-library-body.scm")
  (begin
    (define cached-value 0)
    (define hidden 99)
    (set! cached-value (+ cached-value 1))
    (define-syntax plus-forty
      (syntax-rules ()
        ((_ value) (+ value 40))))))
