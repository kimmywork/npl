; SRFI 178 bitvectors with the canonical NPL library name.
;
; A bitvector is a view over a bytevector: a store, a starting bit offset, and
; a length. Bit 0 of a byte is its least significant bit, matching the
; integers-as-bits convention of SRFI 151. Operations that return a bitvector
; allocate a fresh store unless they are the linear-update variants.
(define-library (npl data bitvector)
  (export
    bit->integer bit->boolean
    make-bitvector bitvector bitvector-unfold bitvector-unfold-right
    bitvector-copy bitvector-reverse-copy
    bitvector-append bitvector-concatenate bitvector-append-subbitvectors
    bitvector? bitvector-empty? bitvector=?
    bitvector-ref/int bitvector-ref/bool bitvector-length
    bitvector-take bitvector-take-right bitvector-drop bitvector-drop-right
    bitvector-segment
    bitvector-fold/int bitvector-fold/bool
    bitvector-fold-right/int bitvector-fold-right/bool
    bitvector-map/int bitvector-map/bool bitvector-map!/int bitvector-map!/bool
    bitvector-map->list/int bitvector-map->list/bool
    bitvector-for-each/int bitvector-for-each/bool
    bitvector-prefix-length bitvector-suffix-length
    bitvector-prefix? bitvector-suffix?
    bitvector-pad bitvector-pad-right
    bitvector-trim bitvector-trim-right bitvector-trim-both
    bitvector-set! bitvector-swap! bitvector-reverse!
    bitvector-copy! bitvector-reverse-copy!
    bitvector->list/int bitvector->list/bool
    reverse-bitvector->list/int reverse-bitvector->list/bool
    list->bitvector reverse-list->bitvector
    bitvector->vector/int bitvector->vector/bool
    reverse-bitvector->vector/int reverse-bitvector->vector/bool
    vector->bitvector reverse-vector->bitvector
    bitvector->string string->bitvector
    bitvector->integer integer->bitvector
    make-bitvector-accumulator
    make-bitvector/int-generator make-bitvector/bool-generator
    bitvector-not bitvector-not!
    bitvector-and bitvector-and! bitvector-ior bitvector-ior!
    bitvector-xor bitvector-xor! bitvector-eqv bitvector-eqv!
    bitvector-nand bitvector-nand! bitvector-nor bitvector-nor!
    bitvector-andc1 bitvector-andc1! bitvector-andc2 bitvector-andc2!
    bitvector-orc1 bitvector-orc1! bitvector-orc2 bitvector-orc2!
    bitvector-logical-shift
    bitvector-count bitvector-count-run bitvector-if bitvector-first-bit
    bitvector-field-any? bitvector-field-every?
    bitvector-field-clear bitvector-field-clear!
    bitvector-field-set bitvector-field-set!
    bitvector-field-replace bitvector-field-replace!
    bitvector-field-replace-same bitvector-field-replace-same!
    bitvector-field-rotate bitvector-field-flip bitvector-field-flip!)
  (import (scheme base) (npl data generator))
  (include "bitvector/core.scm" "bitvector/bitwise.scm"))
