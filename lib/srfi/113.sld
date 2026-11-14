; SRFI 113 compatibility alias for (npl data set) and (npl data bag).
(define-library (srfi 113)
  (export
    set set-contains? set-unfold set? set-empty? set-disjoint? set-member
    set-element-comparator set-adjoin set-adjoin! set-replace set-replace!
    set-delete set-delete! set-delete-all set-delete-all! set-search! set-size
    set-find set-count set-any? set-every? set-map set-for-each set-fold
    set-filter set-filter! set-remove set-remove! set-partition set-partition!
    set-copy set->list list->set list->set! set=? set<? set>? set<=? set>=?
    set-union set-intersection set-difference set-xor set-union!
    set-intersection! set-difference! set-xor! set-comparator bag
    bag-contains? bag-unfold bag? bag-empty? bag-disjoint? bag-member
    bag-element-comparator bag-adjoin bag-adjoin! bag-replace bag-replace!
    bag-delete bag-delete! bag-delete-all bag-delete-all! bag-search! bag-size
    bag-find bag-count bag-any? bag-every? bag-map bag-for-each bag-fold
    bag-filter bag-filter! bag-remove bag-remove! bag-partition bag-partition!
    bag-copy bag->list list->bag list->bag! bag=? bag<? bag>? bag<=? bag>=?
    bag-union bag-intersection bag-difference bag-xor bag-union!
    bag-intersection! bag-difference! bag-xor! bag-comparator bag-unique-size
    bag-sum bag-sum! bag-product bag-product! bag-element-count
    bag-for-each-unique bag-fold-unique bag-increment! bag-decrement! bag->set
    set->bag set->bag! bag->alist alist->bag)
  (import (npl data set) (npl data bag)))
