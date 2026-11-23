# Time

`(npl time)` / `(srfi 19)` provides SRFI 19 time values and date conversion
on top of the F2 C-struct layer. `struct timespec` and `struct tm` layouts are
discovered by `define-c-struct` when the library is expanded; clock ids and
`time_t` conversion stay in `runtime/clock.c`.

## Implemented

- `time-utc`, `time-monotonic`, `time-duration` and `make-time`
- `time?`, `time-type`, `time-second`, `time-nanosecond`, type predicates
- `current-time`, `time-resolution`
- `time-utc->date`, `date->time-utc`, `current-date`
- `date`, its field accessors, `make-date`
- `date->string` for `~Y ~m ~d ~H ~M ~S ~N ~j ~a ~A ~b ~B ~z ~D ~T ~c ~% ~~`
- `time-difference`, `add-duration`, `subtract-duration`

```scheme
(import (scheme base) (npl time))

(define epoch
  (time-utc->date (make-time time-utc 0 0) 0))
(date->string epoch "~Y-~m-~d ~H:~M:~S ~z")
;; => "1970-01-01 00:00:00 +0000"
```

## Not Yet Implemented

`string->date`, the complete SRFI 19 format/parse surface, Julian-day
conversions, and timezone database edge cases. `current-date` defaults to the
host local timezone; pass an explicit seconds offset for a fixed zone.

The nplc/npli fixture `time` checks fixed-epoch conversions and formatting;
the compiler suite `srfi-19` covers the implemented surface.
