(define-library (npl evaluator)
  (export make-eval-environment eval-in-environment
          set-eval-standard-root!
          eval-error? eval-error-message
          eval-unspecified? eval-values? eval-values-list)
  (import (scheme base)
          (scheme write)
          (npl frontend))
  (include "evaluator-core.scm"))
