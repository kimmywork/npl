; Maybe half of SRFI 189.
(define-library (npl data maybe)
  (export maybe? just nothing nothing? just? maybe= maybe-ref maybe-ref/default
          maybe-join maybe-bind maybe-compose maybe-length maybe-filter
          maybe-remove maybe-sequence maybe->either list->just maybe->list
          maybe->truth truth->maybe maybe->values maybe->two-values values->maybe
          maybe-for-each maybe->generation generation->maybe list->maybe
          maybe->list-truth list-truth->maybe tri-not tri=? tri-and tri-or
          tri-merge maybe-map maybe-fold maybe-unfold maybe-and maybe-or
          maybe-let* maybe-let*-values maybe-if two-values->maybe)
  (import (npl data option)))
