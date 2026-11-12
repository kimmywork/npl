; SRFI 196 compatibility alias for (npl data range).
(define-library (srfi 196)
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
  (import (npl data range)))
