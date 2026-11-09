; SRFI 69 basic hash tables: the SRFI 69 names of (srfi 125), which keeps
; them as deprecated synonyms. Two differ from their SRFI 125 synonyms:
; hash-table-copy returns a mutable table, and hash-table-merge! adds every
; association of the second table, replacing values for shared keys (SRFI
; 125 hash-table-union! keeps the first table's values), as Guile does.
(define-library (srfi 69)
  (export
    make-hash-table hash-table? alist->hash-table
    hash-table-equivalence-function hash-table-hash-function
    hash-table-ref hash-table-ref/default hash-table-set! hash-table-delete!
    hash-table-exists? hash-table-update! hash-table-update!/default
    hash-table-size hash-table-keys hash-table-values hash-table-walk
    hash-table-fold hash-table->alist hash-table-copy hash-table-merge!
    hash string-hash string-ci-hash hash-by-identity)
  (import (scheme base)
          (rename (except (srfi 125) hash-table-merge!)
                  (hash-table-copy srfi-125-copy)))
  (begin
    (define (hash-table-copy table) (srfi-125-copy table #t))
    (define (hash-table-merge! table1 table2)
      (hash-table-walk table2
        (lambda (key value) (hash-table-set! table1 key value)))
      table1)))
