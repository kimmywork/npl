; R18: define-c-struct accepts a typedef name; the layout probe has the
; system headers, but the generated stub now only forward-declares
; "struct X" names, so the stub does not compile.  At 32cfa51 the stub
; included <sys/time.h> and this program built.
; Expected: 4 or another size, not a stub compile error.
(import (scheme base) (scheme write) (npl ffi))
(define-c-struct fds "fd_set" (fds_bits i32))
(define-c-function c-zero "bzero" (struct:fds long) -> void)
(define value (make-c-struct fds))
(c-zero value 4)
(display (> (c-struct-size value) 0))
(newline)
