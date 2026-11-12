; SRFI 111 compatibility alias for (npl data box).
(define-library (srfi 111)
  (export box box? unbox set-box!)
  (import (npl data box)))
