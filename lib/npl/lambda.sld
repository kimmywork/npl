; Lambda shorthand with the canonical NPL library name.
;
; Both λ and fn accept an ordinary lambda formal list or arrow segments:
;   (λ x y => body)             one lambda with two formals
;   (λ x => y => body)          nested lambdas
;   (λ (x y) => body)           parenthesized formals
;   (λ (first . rest) => body)  dotted formals
; A collect pass stops at each => and starts the next nested λ with the
; remaining arrow segment.  A bare symbol formal without => is treated as a
; one-element formal list; use a parenthesized dotted list for a rest formal.
(define-library (npl lambda)
  (export λ fn)
  (import (scheme base))
  (begin
    (define-syntax λ
      (syntax-rules (=>)
        ((_ "collect" (args ...) => arg => rest ...)
         (lambda (args ...) (λ arg => rest ...)))
        ((_ "collect" (args ...) => body ...)
         (lambda (args ...) body ...))
        ((_ "collect" (args ...) arg rest ...)
         (λ "collect" (args ... arg) rest ...))
        ((_ "collect" (args ...) body ...)
         (lambda (args ...) body ...))
        ((_ (formals ...) => arg => rest ...)
         (lambda (formals ...) (λ arg => rest ...)))
        ((_ (formals ...) => body ...)
         (lambda (formals ...) body ...))
        ((_ (arg . rest) => next => rest2 ...)
         (lambda (arg . rest) (λ next => rest2 ...)))
        ((_ (arg . rest) => body ...)
         (lambda (arg . rest) body ...))
        ((_ (formals ...) body ...)
         (lambda (formals ...) body ...))
        ((_ (arg . rest) body ...)
         (lambda (arg . rest) body ...))
        ((_ => body ...) (lambda () body ...))
        ((_ arg rest ...)
         (λ "collect" (arg) rest ...))))
    (define-syntax fn
      (syntax-rules ()
        ((_ . arguments) (λ . arguments))))))
