(define-library (npl evaluator)
  (export make-eval-environment make-eval-environment-from-imports
          eval-in-environment eval-load-file
          set-eval-standard-root!
          eval-error? eval-error-message
          eval-unspecified? eval-values? eval-values-list
          eval-callable? eval-apply-in-environment)
  (import (except (scheme base) + - * = < <= > >= number->string append
                  string=?)
          (scheme write)
          (npl primitive)
          (npl frontend))
  (include "evaluator-core.scm" "evaluator-control.scm"
           "evaluator-primitives.scm" "evaluator-records.scm"
           "evaluator-session.scm"))
