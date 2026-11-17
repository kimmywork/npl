; Lambda shorthand with the canonical NPL library name.
;
; Both λ and fn accept an ordinary lambda formal list or the arrow form
; (λ x y => body). Nested currying uses nested lambdas explicitly; the
; arrow form currently has one => per lambda.
(define-library (npl lambda)
  (export λ fn)
  (import (scheme base))
  (begin
    (define-syntax λ
      (syntax-rules (=>)
        ((_ "collect" (argument ...) => body ...)
         (lambda (argument ...) body ...))
        ((_ "collect" (argument ...) next rest ...)
         (λ "collect" (argument ... next) rest ...))
        ((_ => body ...) (lambda () body ...))
        ((_ () => body ...) (lambda () body ...))
        ((_ (argument0 argument ... . rest) => body ...)
         (lambda (argument0 argument ... . rest) body ...))
        ((_ (formals ...) body ...)
         (lambda (formals ...) body ...))
        ((_ (argument0 argument ... . rest) body ...)
         (lambda (argument0 argument ... . rest) body ...))
        ((_ argument0 rest ...)
         (λ "collect" (argument0) rest ...))))
    (define-syntax fn
      (syntax-rules ()
        ((_ . arguments) (λ . arguments))))))
