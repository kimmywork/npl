; R14: npl_ffi_struct_pointer checks the descriptor type but not liveness,
; so a struct freed with free-c-struct reaches C as NULL.
; Expected: (error ...).  At 1b1f931 nplc exits with SIGSEGV after "after
; free"; npli needs the stubs (-l) and reports a missing stub instead.
(import (scheme base) (scheme write) (npl ffi))
(define-c-struct tm "struct tm" (tm_sec int) (tm_year int))
(define-c-function c-gmtime-r "gmtime_r" (pointer struct:tm) -> pointer)
(define seconds (foreign-alloc 8))
(define value (make-c-struct tm))
(c-gmtime-r seconds value)
(display (c-struct-ref value 'tm_year))
(newline)
(free-c-struct value)
(display "after free")
(newline)
(flush-output-port)
(write (guard (e (#t (list 'error (error-object-message e))))
         (c-gmtime-r seconds value)))
(newline)
