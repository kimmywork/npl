(define-library (scheme internal evaluator)
  (export make-eval-environment make-eval-environment-from-imports
          eval-in-environment eval-load-file
          set-eval-standard-root!
          eval-unspecified? eval-environment? eval-condition-text)
  (import (scheme base)
          (scheme read)
          (scheme file)
          (scheme write)
          (only (npl internal primitive) npl_freeze_literal)
          (npl internal frontend))
  (include "evaluator-compile.scm" "evaluator-session.scm"))
