(define-library (scheme load)
  (export load)
  (import (only (scheme internal eval-runtime) load-file))
  (begin
    (define (load filename)
      (load-file filename))))
