; Pattern matching (.agents/archive/design/design-data.md section 4): match, match-lambda,
; match-lambda*, match-let, match-let*, match-letrec, and match-define, the
; Wright/Cartwright/Shinn interface of Guile's (ice-9 match). The macros
; are derived syntax (src/compiler/frontend/derived-match.npl), so
; programs without imports have them too.
(define-library (npl match)
  (export match match-lambda match-lambda* match-let match-let* match-letrec
          match-define)
  (import (npl primitive)))
