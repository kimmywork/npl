; SRFI 95 compatibility alias for (npl sort).
(define-library (srfi 95)
  (export
    sorted? merge merge! sort sort!)
  (import (npl sort)))
