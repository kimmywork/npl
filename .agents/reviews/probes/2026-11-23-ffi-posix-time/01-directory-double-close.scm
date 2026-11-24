; R1: a second close-directory passes a freed DIR* to closedir.
; Expected after the fix: closed-twice is printed (or a catchable error).
(import (scheme base) (scheme write) (npl unix))
(define h (open-directory "."))
(close-directory h)
(display "closed-once") (newline) (flush-output-port)
(close-directory h)
(display "closed-twice") (newline)
