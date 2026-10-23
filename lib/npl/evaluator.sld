(define-library (npl evaluator)
  (export make-eval-environment make-eval-environment-from-imports
          eval-in-environment eval-load-file
          set-eval-standard-root!
          eval-unspecified? eval-environment? eval-condition-text)
  (import (scheme base)
          (scheme read)
          (scheme file)
          (scheme write)
          (npl frontend))
  (include "evaluator-compile.scm" "evaluator-session.scm"))
