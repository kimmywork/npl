; R8: runtime/npl.h declares npl_time_now_ts, but only the c-extension test
; fixture defines it.  Compiled with nplc as an executable, the link fails.
(import (scheme base) (scheme write) (npl ffi))
(define-c-struct timespec "struct timespec" (tv_sec long) (tv_nsec long))
(define-c-function time-now "npl_time_now_ts" (int struct:timespec) -> int)
(write (time-now 0 (make-c-struct timespec)))
(newline)
