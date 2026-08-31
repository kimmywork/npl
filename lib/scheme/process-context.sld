(define-library (scheme process-context)
  (export command-line exit emergency-exit
          get-environment-variable get-environment-variables)
  (import
    (rename
      (only (npl primitive)
            program-arguments exit
            get-environment-variable get-environment-variables)
      (program-arguments command-line)))
  (begin
    (define (emergency-exit code) (exit code))))
