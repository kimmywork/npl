(define-library (npl eval-runtime)
  (export make-environment evaluate)
  (import (only (scheme base) raise)
          (npl evaluator))
  (begin
    (define (raise-eval-error result)
      (if (eval-error? result) (raise result) result))

    (define (wrap-eval-result result environment)
      (if (eval-callable? result)
          (lambda arguments
            (wrap-eval-result
              (raise-eval-error
                (eval-apply-in-environment result arguments environment))
              environment))
          result))

    (define (make-environment import-sets)
      (raise-eval-error
        (make-eval-environment-from-imports import-sets)))

    (define (evaluate expression environment)
      (wrap-eval-result
        (raise-eval-error (eval-in-environment expression environment))
        environment))))
