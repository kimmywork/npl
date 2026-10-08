(define-library (scheme process-context)
  (export command-line exit emergency-exit
          get-environment-variable get-environment-variables)
  (import
    (only (npl primitive)
          command-line exit
          get-environment-variable get-environment-variables))
  (begin
    (define (emergency-exit code) (exit code))))
