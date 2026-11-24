; SRFI 19 compatibility alias for (npl time).
(define-library (srfi 19)
  (export time-utc time-monotonic time-duration time-tai time-thread
          make-time time? time-type time-second time-nanosecond
          time-utc? time-monotonic? time-duration? time-tai? time-thread?
          current-time time-resolution
          time-utc->date date->time-utc current-date
          date? make-date date-nanosecond date-second date-minute
          date-hour date-day date-month date-year date-zone-offset
          date-week-day date-year-day date-dst? date-week-number
          time-difference add-duration subtract-duration
          date->string string->date
          current-julian-day current-modified-julian-day
          date->julian-day date->modified-julian-day
          time-utc->julian-day time-utc->modified-julian-day
          julian-day->date julian-day->time-utc
          modified-julian-day->date modified-julian-day->time-utc
          date->time-monotonic date->time-tai
          time-utc->time-monotonic time-utc->time-tai
          time-monotonic->date time-monotonic->time-utc
          time-monotonic->julian-day time-monotonic->modified-julian-day
          time-tai->date time-tai->time-utc
          time-tai->julian-day time-tai->modified-julian-day
          julian-day->time-monotonic julian-day->time-tai
          modified-julian-day->time-monotonic
          modified-julian-day->time-tai)
  (import (npl time)))
