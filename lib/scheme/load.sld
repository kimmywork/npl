(define-library (scheme load)
  (export load)
  (import (only (npl eval-runtime) load-file))
  (begin
    (define (load filename)
      (load-file filename))))
