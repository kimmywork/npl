; SRFI 47 compatibility alias for (npl data array).
(define-library (srfi 47)
  (export
    array? make-array make-shared-array array-rank array-dimensions
    array-in-bounds? array-ref array-set! ac64 ac32 ar64 ar32 as64 as32 as16
    as8 au64 au32 au16 au8 at1)
  (import (npl data array)))
