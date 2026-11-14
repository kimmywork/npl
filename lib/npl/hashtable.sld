; SRFI 125 intermediate hash tables with the canonical NPL library name
; (docs/reference/srfi-125.html, .agents/archive/design/design-data.md
; section 3) over the runtime tables of runtime/hashtable.c.
;
; Without SRFI 128, the constructors take an equality predicate and an
; optional hash function; a comparator argument is an error. eq?, eqv?,
; equal?, and string=? select the built-in kinds, which hash in C and ignore
; a given hash function (SRFI 125 allows this for refinements of equal?);
; string-ci=? and any other predicate with a hash function make a general
; table. With no argument, make-hash-table uses equal?. The same predicate
; stands for a comparator in hash-table=? and hash-table-map.
;
; Tables made by hash-table and copied without mutable? are immutable;
; their mutators raise error objects. Iteration order is the slot order.
(define-library (npl hashtable)
  (export
    make-hash-table hash-table hash-table-unfold alist->hash-table
    hash-table? hash-table-contains? hash-table-exists? hash-table-empty?
    hash-table=? hash-table-mutable?
    hash-table-ref hash-table-ref/default
    hash-table-set! hash-table-delete! hash-table-intern! hash-table-update!
    hash-table-update!/default hash-table-pop! hash-table-clear!
    hash-table-size hash-table-keys hash-table-values hash-table-entries
    hash-table-find hash-table-count
    hash-table-map hash-table-for-each hash-table-walk hash-table-map!
    hash-table-map->list hash-table-fold hash-table-prune!
    hash-table-copy hash-table-empty-copy hash-table->alist
    hash-table-union! hash-table-merge! hash-table-intersection!
    hash-table-difference! hash-table-xor!
    hash string-hash string-ci-hash hash-by-identity hash-by-eqv
    hash-table-equivalence-function hash-table-hash-function)
  (import (scheme base) (scheme char)
          (only (npl internal primitive)
                npl_make_hash_table npl_hash_table_p npl_hash_table_ref
                npl_hash_table_set npl_hash_table_delete
                npl_hash_table_contains_p npl_hash_table_size
                npl_hash_table_keys npl_hash_table_values
                npl_hash_table_items npl_hash_table_clear
                npl_hash_table_copy npl_hash_table_pop npl_hash_table_info
                npl_hash_value npl_same_procedure_p))
  (include "hashtable.scm"))
