; SRFI 149 is a syntax-rules semantics extension. NPL's core syntax-rules
; implements the SRFI 149 template rules and advertises
; srfi-149-compatible; this library exists for feature detection. The
; syntax-rules keyword itself is core syntax, not a library binding.
(define-library (srfi 149)
  (export syntax-rules)
  (import (scheme base)))
