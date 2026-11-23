; SRFI 19 compatibility alias for (npl time).
(define-library (srfi 19)
  (export time-utc time-monotonic time-duration
          make-time time? time-type time-second time-nanosecond
          time-utc? time-monotonic? time-duration?
          current-time time-resolution
          time-utc->date date->time-utc current-date
          date? make-date date-nanosecond date-second date-minute
          date-hour date-day date-month date-year date-zone-offset
          date-week-day date-year-day date-dst?
          time-difference add-duration subtract-duration
          date->string)
  (import (npl time)))
