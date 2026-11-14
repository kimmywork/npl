; SRFI 128 compatibility alias for (npl comparator).
(define-library (srfi 128)
  (export
    comparator? comparator-ordered? comparator-hashable? make-comparator
    make-pair-comparator make-list-comparator make-vector-comparator
    make-eq-comparator make-eqv-comparator make-equal-comparator boolean-hash
    char-hash char-ci-hash string-hash string-ci-hash symbol-hash number-hash
    hash-bound hash-salt make-default-comparator default-hash
    comparator-register-default! comparator-type-test-predicate
    comparator-equality-predicate comparator-ordering-predicate
    comparator-hash-function comparator-test-type comparator-check-type
    comparator-hash =? <? >? <=? >=? comparator-if<=> comparator-max
    comparator-min comparator-max-in-list comparator-min-in-list
    default-comparator boolean-comparator real-comparator char-comparator
    char-ci-comparator string-comparator string-ci-comparator list-comparator
    vector-comparator eq-comparator eqv-comparator equal-comparator)
  (import (npl comparator)))
