(define-library (scheme eval)
  (export environment eval)
  (import (only (scheme base) cons eq? raise)
          (only (scheme internal eval-runtime) make-environment evaluate))
  (begin
    (define environment-key (cons #f #f))

    (define (environment . import-sets)
      (let ((engine-environment (make-environment import-sets)))
        (lambda (key)
          (if (eq? key environment-key)
              engine-environment
              (raise "environment: invalid access")))))

    (define (eval expression environment-specifier)
      (evaluate expression
                (environment-specifier environment-key)))))
