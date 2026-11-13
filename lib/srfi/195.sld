; SRFI 195 compatibility alias for (npl data multiple-value-box).
(define-library (srfi 195)
  (export
    box box? unbox set-box! box-arity unbox-value set-box-value!)
  (import (npl data multiple-value-box)))
