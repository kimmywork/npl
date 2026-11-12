; SRFI 111 boxes with the canonical NPL library name.
(define-library (npl data box)
  (export box box? unbox set-box!)
  (import (scheme base))
  (begin
    (define-record-type box-type
      (box value)
      box?
      (value unbox set-box!))))
