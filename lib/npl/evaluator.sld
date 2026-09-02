(define-library (npl evaluator)
  (export make-eval-environment make-eval-environment-from-imports
          eval-in-environment
          set-eval-standard-root!
          eval-error? eval-error-message
          eval-unspecified? eval-values? eval-values-list
          eval-callable? eval-apply-in-environment)
  (import (scheme base)
          (scheme write)
          (npl frontend))
  (include "evaluator-core.scm"))
