# Time

`(npl time)` / `(srfi 19)` implements the SRFI 19 time and date surface on
top of `struct timespec` / `struct tm` probes. The native entry points live
in `lib/npl/time/native/time.c`; CMake builds `libnpl_time` as a core
package (linked mode A), so importing the library needs no user flags.

## Implemented

- Time values: `time-utc`, `time-monotonic`, `time-duration`, `time-tai`,
  `time-thread`, `make-time`, accessors, and type predicates.
- `current-time`, `time-resolution` (nanoseconds).
- `time-utc->date`, `date->time-utc`, `current-date`, `make-date`, date
  accessors, `date-week-number`.
- `date->string` with the SRFI 19 directives, including `~a ~A ~b ~B ~c ~d
  ~D ~e ~f ~h ~H ~I ~j ~k ~l ~m ~M ~n ~N ~p ~r ~s ~S ~t ~T ~u ~U ~V ~w ~W
  ~x ~X ~y ~Y ~z ~1 ~2 ~3 ~4 ~5 ~~`. `~Z` returns an empty string because
  no timezone name database is included.
- `string->date` for the Table 2 converters: `~~ ~a ~A ~b ~B ~d ~e ~h ~H
  ~k ~m ~M ~p ~S ~w ~u ~y ~Y ~z ~n ~t ~N`, plus literal text.
- Duration arithmetic: `time-difference`, `add-duration`,
  `subtract-duration`.
- Julian day: `current-julian-day`, `current-modified-julian-day`,
  `date->julian-day`, `julian-day->date`, the modified variants, and the
  UTC/Tai/monotonic converters.

```scheme
(import (scheme base) (npl time))

(define epoch (time-utc->date (make-time time-utc 0 0) 0))
(date->string epoch "~Y-~m-~d ~H:~M:~S ~z")
;; => "1970-01-01 00:00:00 +0000"

(string->date "2001-02-03 04:05:06 +0000"
              "~Y-~m-~d ~H:~M:~S ~z")
```

## Notes

- TAI is POSIX time: no leap-second table is included. Monotonic values keep
  their type and are not converted through UTC by the type aliases.
- `string->date` defaults unparsed fields to 1970-01-01 00:00:00 UTC and a
  zero zone offset.
- `current-date` with no argument uses the host local timezone.
- Tests: `srfi-19` (30 checks) and the nplc/npli `time` fixture.
