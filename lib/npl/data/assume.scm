; Shared SRFI 189/196 precondition check.
;
; The sample implementations use ASSUME for argument and boundary checks.
; Keep one implementation; each library includes this source in its own
; scope, so the check does not add a library dependency.
(define (assume expression . message)
  (if expression
      #t
      (if (null? message)
          (error "assumption failed")
          (apply error (car message) (cdr message)))))
