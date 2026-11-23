; R4: a pointer+offset view keeps its own size, so it still passes the
; bounds check after the owner is freed and reads released memory.
; Expected after the fix: (error ...) for the read through the view.
(import (scheme base) (scheme write) (npl ffi))
(define p (foreign-alloc 16))
(pointer-set-u8! p 7 8)
(define v (pointer+offset p 8))
(foreign-free p)
(write (list 'owner-size (foreign-size p) 'view-size (foreign-size v)))
(newline)
(write (guard (e (#t (list 'error (error-object-message e))))
         (pointer-ref-u8 v 0)))
(newline)
