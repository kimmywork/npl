(define-library (scheme process-context)
  (export command-line exit)
  (import
    (rename (only (npl primitive) program-arguments exit)
            (program-arguments command-line))))
