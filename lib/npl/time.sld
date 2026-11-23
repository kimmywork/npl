; SRFI 19 time, built on F2 C structs (struct timespec / struct tm).
;
; The probe in define-c-struct runs when this library is expanded and uses
; the host C compiler once; the resulting layout descriptor is a quoted value.
; Clock ids and time_t conversions stay in runtime/clock.c, so the public
; values are ordinary NPL records and both nplc and npli use the same path.
(define-library (npl time)
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
  (import (scheme base)
          (npl ffi)
          (only (npl internal primitive)
                npl_time_now_prim npl_time_resolution_prim
                npl_time_gmtime_prim npl_time_localtime_prim
                npl_time_mktime_prim npl_time_timegm_prim))
  (begin
    (define time-utc 'time-utc)
    (define time-monotonic 'time-monotonic)
    (define time-duration 'time-duration)

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

    (define (clock-selector type)
      (cond ((eq? type 'time-utc) 0)
            ((eq? type 'time-monotonic) 1)
            (else (error "time: expected time-utc or time-monotonic" type))))

    (define (current-time . rest)
      (let ((type (if (null? rest) 'time-utc (car rest))))
        (let ((value (make-c-struct timespec)))
          (if (not (= (npl_time_now_prim (clock-selector type) value) 0))
              (error "current-time: clock unavailable"))
          (make-time-record type
                            (c-struct-ref value 'tv_sec)
                            (c-struct-ref value 'tv_nsec)))))

    (define (time-resolution . rest)
      (let ((type (if (null? rest) 'time-utc (car rest))))
        (let ((value (make-c-struct timespec)))
          (if (not (= (npl_time_resolution_prim (clock-selector type) value) 0))
              (error "time-resolution: clock unavailable"))
          (make-time-record 'time-duration
                            (c-struct-ref value 'tv_sec)
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

    (define (make-date nanosecond second minute hour day month year
                       zone-offset . rest)
      (make-date-record nanosecond second minute hour day month year
                        zone-offset
                        (if (null? rest) 0 (car rest))
                        (if (null? (cdr rest)) 0 (car (cdr rest)))
                        (if (null? (cdr (cdr rest))) #f
                            (car (cdr (cdr rest))))))

    (define (time-utc->date value . rest)
      (if (not (time-utc? value))
          (error "time-utc->date: expected a time-utc" value))
      (let ((offset (if (null? rest) #f (car rest)))
            (tm-value (make-c-struct tm))
            (ts-value (make-c-struct timespec)))
        (c-struct-set! ts-value 'tv_sec (time-second value))
        (c-struct-set! ts-value 'tv_nsec (time-nanosecond value))
        (if offset
            (begin
              (c-struct-set! ts-value 'tv_sec
                             (- (time-second value) offset))
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
          (= (c-struct-ref tm-value 'tm_isdst) 1))))

    (define (date->time-utc value)
      (if (not (date? value))
          (error "date->time-utc: expected a date" value))
      (let ((tm-value (make-c-struct tm))
            (ts-value (make-c-struct timespec)))
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
                          (date-nanosecond value))))

    (define (current-date . rest)
      (if (null? rest)
          (time-utc->date (current-time))
          (time-utc->date (current-time) (car rest))))

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

    (define (pad-number value width)
      (let ((text (number->string (if (< value 0) (- value) value))))
        (if (>= (string-length text) width)
            text
            (string-append
              (make-string (- width (string-length text)) #\0)
              text))))

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
      (cond
        ((char=? spec #\Y) (pad-number (date-year value) 4))
        ((char=? spec #\m) (pad-number (date-month value) 2))
        ((char=? spec #\d) (pad-number (date-day value) 2))
        ((char=? spec #\H) (pad-number (date-hour value) 2))
        ((char=? spec #\M) (pad-number (date-minute value) 2))
        ((char=? spec #\S) (pad-number (date-second value) 2))
        ((char=? spec #\N) (pad-number (date-nanosecond value) 9))
        ((char=? spec #\j) (pad-number (date-year-day value) 3))
        ((char=? spec #\a)
         (substring (vector-ref weekdays (date-week-day value)) 0 3))
        ((char=? spec #\A) (vector-ref weekdays (date-week-day value)))
        ((char=? spec #\b)
         (substring (vector-ref months (- (date-month value) 1)) 0 3))
        ((char=? spec #\B) (vector-ref months (- (date-month value) 1)))
        ((char=? spec #\z) (zone-offset-string (date-zone-offset value)))
        ((char=? spec #\D)
         (string-append (pad-number (date-month value) 2) "/"
                        (pad-number (date-day value) 2) "/"
                        (pad-number (date-year value) 4)))
        ((char=? spec #\T)
         (string-append (pad-number (date-hour value) 2) ":"
                        (pad-number (date-minute value) 2) ":"
                        (pad-number (date-second value) 2)))
        ((char=? spec #\c)
         (string-append (pad-number (date-year value) 4) "-"
                        (pad-number (date-month value) 2) "-"
                        (pad-number (date-day value) 2) " "
                        (pad-number (date-hour value) 2) ":"
                        (pad-number (date-minute value) 2) ":"
                        (pad-number (date-second value) 2)))
        ((char=? spec #\%) "\n")
        ((char=? spec #\~) "~")
        (else (string spec))))

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
                          (string-append out (string character)))))))))))
