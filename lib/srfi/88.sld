; SRFI 88 compatibility alias for (npl keyword).
(define-library (srfi 88)
  (export keyword? keyword->string string->keyword)
  (import (npl keyword)))
