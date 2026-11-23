(define (report name value) (write (list name value)) (newline))

(define epoch (time-utc->date (make-time 'time-utc 0 0) 0))
(report 'epoch
        (list (date-year epoch) (date-month epoch) (date-day epoch)
              (date-hour epoch) (date-minute epoch) (date-second epoch)
              (date-zone-offset epoch)
              (date-week-day epoch)
              (date-year-day epoch)))
(report 'round-trip (time-second (date->time-utc epoch)))
(report 'format (date->string epoch "~Y-~m-~d ~H:~M:~S ~z"))
(report 'resolution (time-duration? (time-resolution)))
(report 'monotonic (time-monotonic? (current-time 'time-monotonic)))
(report 'difference
        (time-second
          (time-difference (make-time 'time-utc 0 10)
                           (make-time 'time-utc 0 3))))
(report 'add
        (time-second
          (add-duration (make-time 'time-utc 0 10)
                        (make-time 'time-duration 0 5))))
