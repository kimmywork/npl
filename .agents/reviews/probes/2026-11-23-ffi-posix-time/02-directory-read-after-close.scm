; R1: read-directory after close-directory reads a freed DIR*.
; Expected after the fix: a catchable error, reported as (error ...).
(import (scheme base) (scheme write) (npl unix))
(define h (open-directory "."))
(close-directory h)
(display "closed") (newline) (flush-output-port)
(write (guard (e (#t (list 'error (error-object-message e))))
         (read-directory h)))
(newline)
