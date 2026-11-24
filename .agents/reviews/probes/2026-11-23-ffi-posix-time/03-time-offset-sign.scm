; R2: time-utc->date with a positive zone offset moves the wrong way.
; SRFI 19 (Guile): (1 3600) then 0.
(import (scheme base) (scheme write) (npl time))
(define d (time-utc->date (make-time time-utc 0 0) 3600))
(write (list (date-hour d) (date-zone-offset d))) (newline)
(write (time-second (date->time-utc d))) (newline)
