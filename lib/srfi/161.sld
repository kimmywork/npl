; SRFI 161 compatibility alias for (npl data unifiable-box).
(define-library (srfi 161)
  (export
    ubox ubox? ubox-ref ubox-set! ubox-unify! ubox-union! ubox-link! ubox=?)
  (import (npl data unifiable-box)))
