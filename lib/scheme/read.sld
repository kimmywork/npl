(define-library (scheme read)
  (export read)
  (import (scheme base) (only (npl primitive) read-result npl_make_record))
  (begin
    ; read-result yields #(#t datum) or #(#f message); a syntax error becomes
    ; a read error object (layout: lib/scheme/error.scm).
    (define (read . port)
      (let ((result (read-result (if (null? port)
                                     (current-input-port)
                                     (car port)))))
        (if (vector-ref result 0)
            (vector-ref result 1)
            (raise (npl_make_record 'npl-error-object 'read
                     (vector-ref result 1) '())))))))
