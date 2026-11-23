(use-modules (srfi srfi-19))
(define d (time-utc->date (make-time time-utc 0 0) 3600))
(write (list (date-hour d) (date-zone-offset d))) (newline)
(write (time-second (date->time-utc d))) (newline)
