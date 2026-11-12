; SRFI 1 list library with the canonical NPL library name.
;
; The portable SRFI 1 reference implementation by Olin Shivers (MIT) is
; split into core, fold/map, and search/deletion files to keep each source
; file below the project's soft line limit. Base R7RS names (cons, list,
; make-list, list-copy, map, for-each, member, assoc, append, reverse,
; list-ref, and the pair selectors) remain in (scheme base); import both
; libraries when the full SRFI 1 surface is wanted.
(define-library (npl list)
  (export
    xcons cons* list-tabulate circular-list iota
    proper-list? circular-list? dotted-list? not-pair? null-list? list=
    first second third fourth fifth sixth seventh eighth ninth tenth
    car+cdr take drop take-right drop-right take! drop-right! split-at split-at!
    last last-pair length+ concatenate append! concatenate! reverse!
    append-reverse append-reverse!
    zip unzip1 unzip2 unzip3 unzip4 unzip5 count
    fold unfold pair-fold reduce fold-right unfold-right pair-fold-right
    reduce-right
    append-map append-map! map! pair-for-each filter-map map-in-order
    filter partition remove filter! partition! remove! find find-tail any
    every list-index take-while drop-while take-while! span break span!
    break!
    delete delete-duplicates delete! delete-duplicates!
    alist-cons alist-copy alist-delete alist-delete!
    lset<= lset= lset-adjoin lset-union lset-union! lset-intersection
    lset-intersection! lset-difference lset-difference! lset-xor lset-xor!
    lset-diff+intersection lset-diff+intersection!)
  (import (except (scheme base) make-list list-copy member assoc)
          (scheme cxr))
  (include "list/core.scm" "list/fold.scm" "list/search.scm"))
