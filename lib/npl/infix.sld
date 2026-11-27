; SRFI 105 support procedures that are not reader syntax.
;
; The reader maps a simple curly-infix list to a prefix call.  Mixed
; expressions become ($nfx$ ...); this library gives that reserved name a
; clear error rather than an unbound-identifier error.  Applications may
; import and replace it with a precedence-aware handler.
(define-library (npl infix)
  (export $nfx$ $bracket-apply$)
  (import (scheme base))
  (begin
    (define-syntax $nfx$
      (syntax-rules ()
        ((_ expression ...)
         (error "mixed curly-infix expression needs a precedence handler"
                '(expression ...)))))

    (define ($bracket-apply$ object . indices)
      (cond
        ((vector? object) (apply vector-ref object indices))
        ((string? object) (apply string-ref object indices))
        ((bytevector? object) (apply bytevector-u8-ref object indices))
        ((list? object) (apply list-ref object indices))
        (else (error "$bracket-apply$: unsupported object" object))))))
