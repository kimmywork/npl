; R20: the in-process layout cache is keyed by C type only.  (npl time)
; declares "struct tm" without tm_zone; a second declaration that needs
; tm_zone gets the first layout and fails.  Present since 32cfa51.
; Expected: a positive struct size.
(import (scheme base) (scheme write) (npl ffi) (npl time))
(define-c-struct my-tm "struct tm" (tm_sec int) (tm_zone pointer))
(display (> (c-struct-size (make-c-struct my-tm)) 0))
(newline)
