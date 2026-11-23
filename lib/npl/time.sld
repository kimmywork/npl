; SRFI 19 time, built on F2 C structs (struct timespec / struct tm).
;
; The probe in define-c-struct runs when this library is expanded and uses
; the host C compiler once; the resulting layout descriptor is a quoted value.
; The package's native entry points live in lib/npl/time/native/time.c and
; are declared below; both nplc and npli use the same path.
(define-library (npl time)
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
  (import (scheme base)
          (scheme char)
          (npl ffi))
  (begin
    (define time-utc 'time-utc)
    (define time-monotonic 'time-monotonic)
    (define time-duration 'time-duration)
    (define time-tai 'time-tai)
    (define time-thread 'time-thread)

    (define-c-struct timespec
      "struct timespec"
      (tv_sec long)
      (tv_nsec long))

    (define-c-struct tm
      "struct tm"
      (tm_sec int)
      (tm_min int)
      (tm_hour int)
      (tm_mday int)
      (tm_mon int)
      (tm_year int)
      (tm_wday int)
      (tm_yday int)
      (tm_isdst int)
      (tm_gmtoff long))

    ; Core package entry points (lib/npl/time/native/time.c).  The driver
    ; links libnpl_time automatically; npli links its shared object.
    (define-c-primitive (npl_time_now_prim (which fixnum) (view any))
      fixnum "npl_time_now_prim")
    (define-c-primitive (npl_time_resolution_prim (which fixnum) (view any))
      fixnum "npl_time_resolution_prim")
    (define-c-primitive (npl_time_gmtime_prim (value any) (out any))
      fixnum "npl_time_gmtime_prim")
    (define-c-primitive (npl_time_localtime_prim (value any) (out any))
      fixnum "npl_time_localtime_prim")
    (define-c-primitive (npl_time_mktime_prim (value any) (out any))
      fixnum "npl_time_mktime_prim")
    (define-c-primitive (npl_time_timegm_prim (value any) (out any))
      fixnum "npl_time_timegm_prim")

    (define-record-type time-record
      (make-time-record type second nanosecond)
      time?
      (type time-type)
      (second time-second)
      (nanosecond time-nanosecond))

    (define (make-time type nanosecond second)
      (make-time-record type second nanosecond))

    (define (time-utc? value)
      (if (time? value) (eq? (time-type value) 'time-utc) #f))

    (define (time-monotonic? value)
      (if (time? value) (eq? (time-type value) 'time-monotonic) #f))

    (define (time-duration? value)
      (if (time? value) (eq? (time-type value) 'time-duration) #f))

    (define (time-tai? value)
      (if (time? value) (eq? (time-type value) 'time-tai) #f))

    (define (time-thread? value)
      (if (time? value) (eq? (time-type value) 'time-thread) #f))

    (define (clock-selector type)
      (cond ((eq? type 'time-utc) 0)
            ((eq? type 'time-monotonic) 1)
            (else (error "time: expected time-utc or time-monotonic" type))))

    (define (current-time . rest)
      (let ((type (if (null? rest) 'time-utc (car rest))))
        (with-resource (value (make-c-struct timespec) free-c-struct)
          (if (not (= (npl_time_now_prim (clock-selector type) value) 0))
              (error "current-time: clock unavailable"))
          (make-time-record type
                            (c-struct-ref value 'tv_sec)
                            (c-struct-ref value 'tv_nsec)))))

    ; SRFI 19 specifies nanoseconds, not a duration object.
    (define (time-resolution . rest)
      (let ((type (if (null? rest) 'time-utc (car rest))))
        (with-resource (value (make-c-struct timespec) free-c-struct)
          (if (not (= (npl_time_resolution_prim (clock-selector type) value) 0))
              (error "time-resolution: clock unavailable"))
          (+ (* (c-struct-ref value 'tv_sec) 1000000000)
             (c-struct-ref value 'tv_nsec)))))

    (define-record-type date-record
      (make-date-record nanosecond second minute hour day month year
                        zone-offset week-day year-day dst?)
      date?
      (nanosecond date-nanosecond)
      (second date-second)
      (minute date-minute)
      (hour date-hour)
      (day date-day)
      (month date-month)
      (year date-year)
      (zone-offset date-zone-offset)
      (week-day date-week-day)
      (year-day date-year-day)
      (dst? date-dst?))

    ; ---- calendar helpers -------------------------------------------------

    (define (leap-year? year)
      (if (= (modulo year 4) 0)
          (if (= (modulo year 100) 0)
              (= (modulo year 400) 0)
              #t)
          #f))

    (define (days-in-month year month)
      (cond
        ((= month 2) (if (leap-year? year) 29 28))
        ((if (= month 4) #t
             (if (= month 6) #t
                 (if (= month 9) #t (= month 11))))
         30)
        (else 31)))

    (define (calendar-year-day year month day)
      (let loop ((month-index 1) (total 0))
        (if (= month-index month)
            (+ total day)
            (loop (+ month-index 1)
                  (+ total (days-in-month year month-index))))))

    ; Zeller's congruence; Sunday = 0.
    (define (calendar-week-day year month day)
      (let* ((adjusted-year (if (< month 3) (- year 1) year))
             (adjusted-month (if (< month 3) (+ month 12) month))
             (k (modulo adjusted-year 100))
             (j (quotient adjusted-year 100))
             (h (modulo (+ day
                           (quotient (* 13 (+ adjusted-month 1)) 5)
                           k
                           (quotient k 4)
                           (quotient j 4)
                           (* 5 j))
                        7)))
        (modulo (+ h 6) 7)))

    (define (make-date nanosecond second minute hour day month year
                       zone-offset . rest)
      (let ((week-day (if (null? rest) #f (car rest)))
            (year-day (if (if (null? rest) #t (null? (cdr rest)))
                          #f
                          (car (cdr rest))))
            (dst? (if (if (null? rest) #t (null? (cdr rest)))
                      #f
                      (if (null? (cdr (cdr rest)))
                          #f
                          (car (cdr (cdr rest)))))))
        (make-date-record nanosecond second minute hour day month year
                          zone-offset
                          (if week-day week-day
                              (calendar-week-day year month day))
                          (if year-day year-day
                              (calendar-year-day year month day))
                          dst?)))

    ; ---- UTC <-> date -----------------------------------------------------

    (define (date->time-utc value)
      (if (not (date? value))
          (error "date->time-utc: expected a date" value))
      (with-resource (tm-value (make-c-struct tm) free-c-struct)
        (with-resource (ts-value (make-c-struct timespec) free-c-struct)
          (c-struct-set! tm-value 'tm_sec (date-second value))
          (c-struct-set! tm-value 'tm_min (date-minute value))
          (c-struct-set! tm-value 'tm_hour (date-hour value))
          (c-struct-set! tm-value 'tm_mday (date-day value))
          (c-struct-set! tm-value 'tm_mon (- (date-month value) 1))
          (c-struct-set! tm-value 'tm_year (- (date-year value) 1900))
          (c-struct-set! tm-value 'tm_isdst (if (date-dst? value) 1 0))
          (if (not (= (npl_time_timegm_prim tm-value ts-value) 0))
              (error "date->time-utc: conversion failed" value))
          (if (not (= (date-zone-offset value) 0))
              (c-struct-set! ts-value 'tv_sec
                             (- (c-struct-ref ts-value 'tv_sec)
                                (date-zone-offset value))))
          (make-time-record 'time-utc
                            (c-struct-ref ts-value 'tv_sec)
                            (date-nanosecond value)))))

    (define (time-utc->date value . rest)
      (if (not (time-utc? value))
          (error "time-utc->date: expected a time-utc" value))
      (let ((offset (if (null? rest) #f (car rest))))
        (with-resource (tm-value (make-c-struct tm) free-c-struct)
          (with-resource (ts-value (make-c-struct timespec) free-c-struct)
            (c-struct-set! ts-value 'tv_sec (time-second value))
            (c-struct-set! ts-value 'tv_nsec (time-nanosecond value))
            (if offset
                (begin
                  (c-struct-set! ts-value 'tv_sec
                                 (+ (time-second value) offset))
                  (if (not (= (npl_time_gmtime_prim ts-value tm-value) 0))
                      (error "time-utc->date: conversion failed")))
                (if (not (= (npl_time_localtime_prim ts-value tm-value) 0))
                    (error "time-utc->date: conversion failed")))
            (make-date-record
              (time-nanosecond value)
              (c-struct-ref tm-value 'tm_sec)
              (c-struct-ref tm-value 'tm_min)
              (c-struct-ref tm-value 'tm_hour)
              (c-struct-ref tm-value 'tm_mday)
              (+ 1 (c-struct-ref tm-value 'tm_mon))
              (+ 1900 (c-struct-ref tm-value 'tm_year))
              (if offset offset (c-struct-ref tm-value 'tm_gmtoff))
              (c-struct-ref tm-value 'tm_wday)
              (+ 1 (c-struct-ref tm-value 'tm_yday))
              (= (c-struct-ref tm-value 'tm_isdst) 1))))))

    (define (current-date . rest)
      (if (null? rest)
          (time-utc->date (current-time))
          (time-utc->date (current-time) (car rest))))

    ; ---- durations --------------------------------------------------------

    (define (normalize-time type second nanosecond)
      (let ((carry (quotient nanosecond 1000000000))
            (rest (modulo nanosecond 1000000000)))
        (make-time-record type (+ second carry) rest)))

    (define (time-difference left right)
      (normalize-time 'time-duration
                      (- (time-second left) (time-second right))
                      (- (time-nanosecond left) (time-nanosecond right))))

    (define (add-duration time duration)
      (normalize-time (if (time? time) (time-type time) 'time-utc)
                      (+ (time-second time) (time-second duration))
                      (+ (time-nanosecond time) (time-nanosecond duration))))

    (define (subtract-duration time duration)
      (normalize-time (if (time? time) (time-type time) 'time-utc)
                      (- (time-second time) (time-second duration))
                      (- (time-nanosecond time) (time-nanosecond duration))))

    ; ---- Julian day conversions ------------------------------------------

    (define JULIAN_DAY_AT_EPOCH 2440587.5)

    (define (time-utc->julian-day value)
      (+ (/ (time-second value) 86400)
         (/ (time-nanosecond value) 86400000000000)
         JULIAN_DAY_AT_EPOCH))

    (define (time-utc->modified-julian-day value)
      (- (time-utc->julian-day value) 2400000.5))

    (define (date->julian-day value)
      (time-utc->julian-day (date->time-utc value)))

    (define (date->modified-julian-day value)
      (- (date->julian-day value) 2400000.5))

    (define (julian-day->time-utc value)
      (let* ((seconds (* (- value JULIAN_DAY_AT_EPOCH) 86400))
             (whole (inexact->exact (floor seconds)))
             (fraction (- seconds whole))
             (nanoseconds (inexact->exact
                           (round (* fraction 1000000000)))))
        (normalize-time 'time-utc whole nanoseconds)))

    (define (modified-julian-day->time-utc value)
      (julian-day->time-utc (+ value 2400000.5)))

    (define (julian-day->date value . rest)
      (time-utc->date (julian-day->time-utc value)
                      (if (null? rest) (date-zone-offset (current-date))
                          (car rest))))

    (define (modified-julian-day->date value . rest)
      (apply julian-day->date (+ value 2400000.5) rest))

    (define (current-julian-day)
      (time-utc->julian-day (current-time)))

    (define (current-modified-julian-day)
      (time-utc->modified-julian-day (current-time)))

    ; ---- TAI / monotonic aliases -----------------------------------------
    ; No leap-second table is available, so TAI is POSIX time (the same
    ; approximation R7RS permits) and monotonic values keep their type.

    (define (retype value type)
      (make-time-record type (time-second value) (time-nanosecond value)))

    (define (date->time-tai value) (retype (date->time-utc value) 'time-tai))
    (define (date->time-monotonic value)
      (retype (date->time-utc value) 'time-monotonic))
    (define (time-utc->time-tai value) (retype value 'time-tai))
    (define (time-utc->time-monotonic value) (retype value 'time-monotonic))
    (define (time-tai->time-utc value) (retype value 'time-utc))
    (define (time-monotonic->time-utc value) (retype value 'time-utc))
    (define (time-tai->date value . rest)
      (apply time-utc->date (retype value 'time-utc)
             (if (null? rest)
                 (list (date-zone-offset (current-date)))
                 rest)))
    (define (time-monotonic->date value . rest)
      (apply time-utc->date (retype value 'time-utc)
             (if (null? rest)
                 (list (date-zone-offset (current-date)))
                 rest)))
    (define (time-tai->julian-day value)
      (time-utc->julian-day (retype value 'time-utc)))
    (define (time-monotonic->julian-day value)
      (time-utc->julian-day (retype value 'time-utc)))
    (define (time-tai->modified-julian-day value)
      (time-utc->modified-julian-day (retype value 'time-utc)))
    (define (time-monotonic->modified-julian-day value)
      (time-utc->modified-julian-day (retype value 'time-utc)))
    (define (julian-day->time-tai value)
      (retype (julian-day->time-utc value) 'time-tai))
    (define (julian-day->time-monotonic value)
      (retype (julian-day->time-utc value) 'time-monotonic))
    (define (modified-julian-day->time-tai value)
      (retype (modified-julian-day->time-utc value) 'time-tai))
    (define (modified-julian-day->time-monotonic value)
      (retype (modified-julian-day->time-utc value) 'time-monotonic))

    ; ---- formatting -------------------------------------------------------

    (define (pad-number value width)
      (let ((text (number->string (if (< value 0) (- value) value))))
        (if (>= (string-length text) width)
            text
            (string-append
              (make-string (- width (string-length text)) #\0)
              text))))

    (define (blank-pad-number value width)
      (let ((text (number->string value)))
        (if (>= (string-length text) width)
            text
            (string-append
              (make-string (- width (string-length text)) #\space)
              text))))

    (define (twelve-hour hour)
      (let ((value (modulo hour 12)))
        (if (= value 0) 12 value)))

    (define (iso-week-number value)
      (let* ((year (date-year value))
             (week-day (date-week-day value))
             (iso-week-day (if (= week-day 0) 7 week-day))
             (year-day (date-year-day value))
             (thursday (- year-day (- 4 iso-week-day))))
        (cond
          ((< thursday 1) (iso-week-number
                            (make-date 0 0 0 0 31 12 (- year 1) 0)))
          ((> thursday (if (leap-year? year) 366 365)) 1)
          (else (quotient (+ thursday 6) 7)))))

    (define (date-week-number value start)
      (let ((offset (modulo (- (date-week-day value) start) 7)))
        (quotient (+ (- (date-year-day value) 1) 7 (- offset)) 7)))

    (define weekdays
      '#("Sunday" "Monday" "Tuesday" "Wednesday" "Thursday" "Friday"
         "Saturday"))
    (define months
      '#("January" "February" "March" "April" "May" "June" "July"
         "August" "September" "October" "November" "December"))

    (define (zone-offset-string offset)
      (let* ((sign (if (< offset 0) "-" "+"))
             (amount (if (< offset 0) (- offset) offset))
             (hours (quotient amount 3600))
             (minutes (quotient (modulo amount 3600) 60)))
        (string-append sign (pad-number hours 2) (pad-number minutes 2))))

    (define (date-format-spec value spec)
      (let ((hour (date-hour value))
            (month (date-month value))
            (week-day (date-week-day value)))
        (cond
          ((char=? spec #\a) (substring (vector-ref weekdays week-day) 0 3))
          ((char=? spec #\A) (vector-ref weekdays week-day))
          ((char=? spec #\b) (substring (vector-ref months (- month 1)) 0 3))
          ((char=? spec #\B) (vector-ref months (- month 1)))
          ((char=? spec #\h) (substring (vector-ref months (- month 1)) 0 3))
          ((char=? spec #\c)
           (date->string value "~a ~b ~e ~H:~M:~S~z ~Y"))
          ((char=? spec #\d) (pad-number (date-day value) 2))
          ((char=? spec #\D)
           (string-append (pad-number month 2) "/"
                          (pad-number (date-day value) 2) "/"
                          (pad-number (modulo (date-year value) 100) 2)))
          ((char=? spec #\e) (blank-pad-number (date-day value) 2))
          ((char=? spec #\f)
           (string-append (pad-number (date-second value) 2) "."
                          (pad-number (date-nanosecond value) 9)))
          ((char=? spec #\H) (pad-number hour 2))
          ((char=? spec #\I) (pad-number (twelve-hour hour) 2))
          ((char=? spec #\j) (pad-number (date-year-day value) 3))
          ((char=? spec #\k) (blank-pad-number hour 2))
          ((char=? spec #\l) (blank-pad-number (twelve-hour hour) 2))
          ((char=? spec #\m) (pad-number month 2))
          ((char=? spec #\M) (pad-number (date-minute value) 2))
          ((char=? spec #\n) "\n")
          ((char=? spec #\N) (pad-number (date-nanosecond value) 9))
          ((char=? spec #\p) (if (< hour 12) "AM" "PM"))
          ((char=? spec #\r)
           (date->string value "~I:~M:~S ~p"))
          ((char=? spec #\s)
           (number->string (time-second (date->time-utc value))))
          ((char=? spec #\S) (pad-number (date-second value) 2))
          ((char=? spec #\t) "\t")
          ((char=? spec #\T)
           (string-append (pad-number hour 2) ":"
                          (pad-number (date-minute value) 2) ":"
                          (pad-number (date-second value) 2)))
          ((char=? spec #\u) (number->string (if (= week-day 0) 7 week-day)))
          ((char=? spec #\U)
           (pad-number (date-week-number value 0) 2))
          ((char=? spec #\V) (pad-number (iso-week-number value) 2))
          ((char=? spec #\w) (number->string week-day))
          ((char=? spec #\W)
           (pad-number (date-week-number value 1) 2))
          ((char=? spec #\x)
           (string-append (pad-number month 2) "/"
                          (pad-number (date-day value) 2) "/"
                          (pad-number (modulo (date-year value) 100) 2)))
          ((char=? spec #\X)
           (string-append (pad-number hour 2) ":"
                          (pad-number (date-minute value) 2) ":"
                          (pad-number (date-second value) 2)))
          ((char=? spec #\y)
           (pad-number (modulo (date-year value) 100) 2))
          ((char=? spec #\Y) (pad-number (date-year value) 4))
          ((char=? spec #\z) (zone-offset-string (date-zone-offset value)))
          ((char=? spec #\Z) "")
          ((char=? spec #\1)
           (string-append (pad-number (date-year value) 4) "-"
                          (pad-number month 2) "-"
                          (pad-number (date-day value) 2)))
          ((char=? spec #\2)
           (date->string value "~H:~M:~S~z"))
          ((char=? spec #\3)
           (date->string value "~H:~M:~S"))
          ((char=? spec #\4)
           (date->string value "~Y-~m-~dT~H:~M:~S~z"))
          ((char=? spec #\5)
           (date->string value "~Y-~m-~dT~H:~M:~S"))
          ((char=? spec #\%) "\n")
          ((char=? spec #\~) "~")
          (else (string spec)))))

    (define (date->string value . rest)
      (if (not (date? value))
          (error "date->string: expected a date" value))
      (let ((format (if (null? rest) "~c" (car rest))))
        (let loop ((index 0) (out ""))
          (if (= index (string-length format))
              out
              (let ((character (string-ref format index)))
                (if (char=? character #\~)
                    (if (>= (+ index 1) (string-length format))
                        (error "date->string: trailing ~" format)
                        (loop (+ index 2)
                              (string-append
                                out
                                (date-format-spec
                                  value
                                  (string-ref format (+ index 1))))))
                    (loop (+ index 1)
                          (string-append out (string character)))))))))

    ; ---- parsing ----------------------------------------------------------

    (define (string-prefix-ci? text prefix)
      (let ((length (if (< (string-length text) (string-length prefix))
                        (string-length text)
                        (string-length prefix))))
        (if (= length 0)
            #f
            (let loop ((index 0))
              (cond
                ((= index length) #t)
                ((char-ci=? (string-ref text index) (string-ref prefix index))
                 (loop (+ index 1)))
                (else #f))))))

    (define (alpha-name-index text names start)
      (let loop ((index 0))
        (if (= index (vector-length names))
            #f
            (if (string-prefix-ci? text (vector-ref names index))
                index
                (loop (+ index 1))))))

    (define (skip-spaces text index)
      (if (if (< index (string-length text))
              (char-whitespace? (string-ref text index))
              #f)
          (skip-spaces text (+ index 1))
          index))

    (define (read-digits text index)
      (let loop ((cursor index))
        (if (if (< cursor (string-length text))
                (char-numeric? (string-ref text cursor))
                #f)
            (loop (+ cursor 1))
            (if (= cursor index)
                (error "string->date: expected a number" text index)
                (cons (string->number (substring text index cursor))
                      cursor)))))

    (define (read-alpha text index)
      (let loop ((cursor index))
        (if (if (< cursor (string-length text))
                (char-alphabetic? (string-ref text cursor))
                #f)
            (loop (+ cursor 1))
            cursor)))

    ; Parse state: year month day hour minute second nanosecond zone
    ; hour12? pm? weekday.
    (define (date-parse-state)
      (vector 1970 1 1 0 0 0 0 0 #f #f #f))

    (define (date-parse-zone input index state)
      (let ((start (skip-spaces input index)))
        (cond
          ((if (< start (string-length input))
               (char-ci=? (string-ref input start) #\z)
               #f)
           (vector-set! state 7 0)
           (+ start 1))
          ((if (< start (string-length input))
               (if (char=? (string-ref input start) #\+) #t
                   (char=? (string-ref input start) #\-))
               #f)
           (let* ((negative (char=? (string-ref input start) #\-))
                  (signed (+ start 1))
                  (after-hours (if (if (< (+ signed 2) (string-length input))
                                      (char=? (string-ref input (+ signed 2)) #\:)
                                      #f)
                                  (+ signed 3)
                                  (+ signed 2)))
                  (hours (if (<= (+ signed 2) (string-length input))
                             (string->number
                               (substring input signed (+ signed 2)))
                             #f))
                  (minutes (if (<= (+ after-hours 2) (string-length input))
                               (string->number
                                 (substring input after-hours
                                            (+ after-hours 2)))
                               #f)))
             (if (if hours minutes #f)
                 (begin
                   (vector-set! state 7
                                (* (if negative -1 1)
                                   (+ (* hours 3600) (* minutes 60))))
                   (+ after-hours 2))
                 (error "string->date: bad zone offset" input start))))
          (else (error "string->date: expected a zone offset" input start)))))

    (define (date-parse-field spec input index state)
      (cond
        ((char=? spec #\~)
         (if (if (< index (string-length input))
                 (char=? (string-ref input index) #\~)
                 #f)
             (+ index 1)
             (error "string->date: expected ~" input)))
        ((memv spec '(#\n #\t))
         (skip-spaces input index))
        ((memv spec '(#\a #\A))
         (let* ((start (skip-spaces input index))
                (end (read-alpha input start))
                (name (substring input start end))
                (weekday (alpha-name-index name weekdays 0)))
           (if (not weekday)
               (error "string->date: bad weekday" name))
           (vector-set! state 10 weekday)
           end))
        ((memv spec '(#\b #\B #\h))
         (let* ((start (skip-spaces input index))
                (end (read-alpha input start))
                (name (substring input start end))
                (month (alpha-name-index name months 0)))
           (if (not month)
               (error "string->date: bad month" name))
           (vector-set! state 1 (+ month 1))
           end))
        ((memv spec '(#\d #\e))
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 2 (car read))
           (cdr read)))
        ((memv spec '(#\H #\k #\I #\l))
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 3 (car read))
           (if (if (char=? spec #\I) #t (char=? spec #\l))
               (vector-set! state 8 #t))
           (cdr read)))
        ((char=? spec #\p)
         (let ((start (skip-spaces input index)))
           (if (if (< start (string-length input))
                   (char-ci=? (string-ref input start) #\p)
                   #f)
               (vector-set! state 9 #t)
               (vector-set! state 9 #f))
           (+ start 2)))
        ((char=? spec #\m)
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 1 (car read))
           (cdr read)))
        ((char=? spec #\M)
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 4 (car read))
           (cdr read)))
        ((char=? spec #\S)
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 5 (car read))
           (cdr read)))
        ((char=? spec #\N)
         (let* ((start (skip-spaces input index))
                (read (read-digits input start))
                (digits (- (cdr read) start))
                (value (car read)))
           (vector-set! state 6
                        (if (>= digits 9)
                            (quotient value (expt 10 (- digits 9)))
                            (* value (expt 10 (- 9 digits)))))
           (cdr read)))
        ((char=? spec #\Y)
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 0 (car read))
           (cdr read)))
        ((char=? spec #\y)
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 0
                        (if (<= (car read) 69)
                            (+ 2000 (car read))
                            (+ 1900 (car read))))
           (cdr read)))
        ((char=? spec #\z) (date-parse-zone input index state))
        ((memv spec '(#\w #\u))
         (let ((read (read-digits input (skip-spaces input index))))
           (vector-set! state 10
                        (if (char=? spec #\u)
                            (modulo (car read) 7)
                            (car read)))
           (cdr read)))
        (else
         (error "string->date: unsupported directive" spec))))

    (define (date-parse-template input template state)
      (let ((input-length (string-length input))
            (template-length (string-length template)))
        (let loop ((input-index 0) (template-index 0))
          (if (= template-index template-length)
              (if (< input-index input-length)
                  (error "string->date: trailing input" input input-index)
                  state)
              (let ((character (string-ref template template-index)))
                (if (char=? character #\~)
                    (if (>= (+ template-index 1) template-length)
                        (error "string->date: trailing ~" template)
                        (loop (date-parse-field
                                (string-ref template (+ template-index 1))
                                input input-index state)
                              (+ template-index 2)))
                    (if (if (< input-index input-length)
                            (char=? (string-ref input input-index) character)
                            #f)
                        (loop (+ input-index 1) (+ template-index 1))
                        (error "string->date: literal mismatch"
                               character input-index))))))))

    (define (string->date input template)
      (let* ((state (date-parse-template input template (date-parse-state)))
             (year (vector-ref state 0))
             (month (vector-ref state 1))
             (day (vector-ref state 2))
             (hour (vector-ref state 3))
             (hour (if (vector-ref state 8)
                       (+ (modulo hour 12) (if (vector-ref state 9) 12 0))
                       hour))
             (week-day (vector-ref state 10)))
        (make-date (vector-ref state 6)
                   (vector-ref state 5)
                   (vector-ref state 4)
                   hour
                   day
                   month
                   year
                   (vector-ref state 7)
                   (if week-day week-day
                       (calendar-week-day year month day))
                   (calendar-year-day year month day)
                   #f)))
))
