(define-library (scheme process-context)
  (export command-line exit emergency-exit
          get-environment-variable get-environment-variables)
  (import
    (only (npl primitive)
          command-line exit
          get-environment-variable get-environment-variables car null?))
  (begin
    (define (emergency-exit . code)
      (if (null? code) (exit) (exit (car code))))))
