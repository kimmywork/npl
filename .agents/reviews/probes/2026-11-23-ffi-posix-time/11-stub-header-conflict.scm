; R10: the stubs now include <time.h>, <sys/stat.h>, <pwd.h>, ... before the
; generated prototype, so a define-c-function for a libc function whose NPL
; types differ from the system prototype no longer compiles.  time(3) takes
; time_t *; NPL declares it with pointer (void *).  On master the stub had
; only the standard headers and this compiled.
(import (scheme base) (scheme write) (npl ffi))
(define-c-function c-time "time" (pointer) -> long)
(write (> (c-time (null-pointer)) 0))
(newline)
