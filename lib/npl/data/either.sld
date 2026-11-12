; Either half of SRFI 189.
(define-library (npl data either)
  (export either? left right left? right? either= either-swap either-ref
          either-ref/default either-join either-bind either-compose
          either-length either-filter either-remove either-sequence
          either->maybe list->right either->list either->truth truth->either
          either->values values->either either-for-each
          either->generation generation->either list->left list->either
          either->list-truth list-truth->either exception->either either-guard
          either-map either-fold either-unfold either-and either-or
          either-let* either-let*-values)
  (import (npl data option)))
