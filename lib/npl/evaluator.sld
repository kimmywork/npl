(define-library (npl evaluator)
  (export make-eval-environment eval-in-environment
          eval-error? eval-error-message
          eval-unspecified? eval-values? eval-values-list)
  (import (scheme base)
          (scheme write))
  (include "evaluator-core.scm"))
