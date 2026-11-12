; SRFI 196 range objects with the canonical NPL library name.
;
; The portable SRFI 196 reference implementation (MIT) is included below.
; It uses the SRFI 1 procedures exported by (npl list). The optional SRFI 145
; assume check is reduced to a no-op.
(define-library (npl data range)
  (export range numeric-range vector-range string-range range-append
          iota-range range? range=? range-length range-ref range-first
          range-last subrange range-segment range-split-at range-take
          range-take-right range-drop range-drop-right range-count
          range-map->list range-for-each range-fold range-fold-right
          range-any range-every range-filter->list range-remove->list
          range-reverse range-map range-map->vector range-filter
          range-remove range-filter-map range-filter-map->list
          range-index range-index-right range-take-while
          range-drop-while range-take-while-right
          range-drop-while-right vector->range range->string
          range->list range->generator range->vector)
  (import (scheme base) (scheme case-lambda) (npl list))
  (begin
    (define (assume . args) #t)
    (define (vector-unfold f len)
      (let ((result (make-vector len)))
        (let loop ((index 0))
          (if (= index len)
              result
              (begin
                (vector-set! result index (f index))
                (loop (+ index 1))))))))
  (include "range.scm"))
