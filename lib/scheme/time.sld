(define-library (scheme time)
  (export current-jiffy current-second jiffies-per-second)
  (import (only (npl primitive)
                npl_current_jiffy npl_current_second npl_jiffies_per_second))
  (begin
    (define (current-jiffy) (npl_current_jiffy))
    (define (current-second) (npl_current_second))
    (define (jiffies-per-second) (npl_jiffies_per_second))))
