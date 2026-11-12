; C pointers and deterministic release for foreign calls (docs/ffi.md).
;
; A pointer is an opaque C address: pointer? tests for one, pointer-null?
; for the null address, eqv? compares addresses, and write shows
; #<pointer 0x...>. The collector never frees what a pointer refers to; the
; C library's own release function does, called explicitly.
;
; (with-resource (name init release) body ...) binds name to init, runs
; body, and calls (release name) exactly once when control leaves body:
; normal return, a raise caught outside, an escape through a continuation,
; or exit (R7RS 6.14). Re-entering body through a continuation does not
; acquire the resource again.
(define-library (npl ffi)
  (export pointer? pointer-null? null-pointer pointer->integer with-resource)
  (import (scheme base)
          (only (npl internal primitive)
                pointer? pointer-null? null-pointer pointer->integer))
  (begin
    (define-syntax with-resource
      (syntax-rules ()
        ((_ (name init release) body1 body2 ...)
         (let ((name init) (open #t))
           (dynamic-wind
             (lambda () #f)
             (lambda () body1 body2 ...)
             (lambda ()
               (if open
                   (begin (set! open #f) (release name)))))))))))
