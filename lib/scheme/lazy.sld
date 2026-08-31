(define-library (scheme lazy)
  (export delay delay-force force make-promise promise?)
  (import (only (scheme base)
                delay delay-force force make-promise promise?)))
