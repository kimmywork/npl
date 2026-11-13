; SRFI 113 sets with the canonical NPL library name.
;
; Sets are mutable hash-table records keyed by the element comparator; the
; non-bang operations copy the table. The table stores the element as both
; key and value so set-member can return the stored object.
(define-library (npl data set)
  (export
    set set-contains? set-unfold
    set? set-empty? set-disjoint?
    set-member set-element-comparator
    set-adjoin set-adjoin! set-replace set-replace!
    set-delete set-delete! set-delete-all set-delete-all!
    set-search!
    set-size set-find set-count set-any? set-every?
    set-map set-for-each set-fold set-filter set-filter!
    set-remove set-remove! set-partition set-partition!
    set-copy set->list list->set list->set!
    set=? set<? set>? set<=? set>=?
    set-union set-intersection set-difference set-xor
    set-union! set-intersection! set-difference! set-xor!
    set-comparator)
  (import (scheme base) (npl list) (npl comparator) (npl hashtable))
  (begin
    (define-record-type set-type
      (make-set table comparator)
      set?
      (table set-table)
      (comparator set-key-comparator))

    (define (set-table-new comparator)
      (make-hash-table
       (comparator-equality-predicate comparator)
       (lambda (element) (comparator-hash comparator element))))

    (define (set-copy-table table comparator)
      (let ((result (set-table-new comparator)))
        (hash-table-for-each
         (lambda (key value) (hash-table-set! result key value))
         table)
        result))

    (define (set comparator . elements)
      (let ((table (set-table-new comparator)))
        (for-each (lambda (element) (hash-table-set! table element element))
                  elements)
        (make-set table comparator)))

    (define (set-unfold comparator stop? mapper successor seed)
      (let loop ((seed seed) (result (set comparator)))
        (if (stop? seed)
            result
            (loop (successor seed) (set-adjoin! result (mapper seed))))))

    (define (set-contains? set element)
      (hash-table-contains? (set-table set) element))

    (define (set-empty? set) (zero? (set-size set)))

    (define (set-disjoint? left right)
      (if (< (hash-table-size (set-table right))
             (hash-table-size (set-table left)))
          (set-disjoint? right left)
          (let ((table (set-table right)))
            (not (hash-table-find
                  (lambda (key value) (hash-table-contains? table key))
                  (set-table left)
                  (lambda () #f))))))

    (define (set-member set element default)
      (hash-table-ref/default (set-table set) element default))

    (define (set-element-comparator set) (set-key-comparator set))

    (define (set-adjoin set . elements)
      (apply set-adjoin! (set-copy set) elements))

    (define (set-adjoin! set . elements)
      (for-each (lambda (element)
                  (hash-table-set! (set-table set) element element))
                elements)
      set)

    (define (set-replace set element)
      (set-replace! (set-copy set) element))

    (define (set-replace! set element)
      (if (hash-table-contains? (set-table set) element)
          (begin
            (hash-table-delete! (set-table set) element)
            (hash-table-set! (set-table set) element element)))
      set)

    (define (set-delete set . elements) (set-delete-all set elements))
    (define (set-delete! set . elements) (set-delete-all! set elements))

    (define (set-delete-all set elements)
      (set-delete-all! (set-copy set) elements))

    (define (set-delete-all! set elements)
      (for-each (lambda (element)
                  (hash-table-delete! (set-table set) element))
                elements)
      set)

    (define set-search!
      (let ((not-found (list 'not-found)))
        (lambda (set element failure success)
          (let ((found (hash-table-ref/default (set-table set) element not-found)))
            (if (eq? found not-found)
                (failure
                 (lambda (object)
                   (hash-table-set! (set-table set) element element)
                   (values set object))
                 (lambda (object) (values set object)))
                (success
                 found
                 (lambda (new-element object)
                   (hash-table-delete! (set-table set) found)
                   (hash-table-set! (set-table set) new-element new-element)
                   (values set object))
                 (lambda (object)
                   (hash-table-delete! (set-table set) element)
                   (values set object))))))))

    (define (set-size set) (hash-table-size (set-table set)))

    (define (set-find predicate set failure)
      (call-with-current-continuation
       (lambda (return)
         (hash-table-for-each
          (lambda (element value)
            (if (predicate element) (return element)))
          (set-table set))
         (failure))))

    (define (set-count predicate set)
      (hash-table-count (lambda (key value) (predicate key)) (set-table set)))

    (define (set-any? predicate set)
      (and (hash-table-find (lambda (key value) (predicate key))
                            (set-table set)
                            (lambda () #f))
           #t))

    (define (set-every? predicate set)
      (not (set-any? (lambda (element) (not (predicate element))) set)))

    (define (set-map comparator proc set-value)
      (set-fold (lambda (element result) (set-adjoin! result (proc element)))
                (set comparator)
                set-value))

    (define (set-for-each proc set)
      (hash-table-for-each (lambda (element value) (proc element))
                           (set-table set)))

    (define (set-fold proc nil set)
      (hash-table-fold (lambda (element value acc) (proc element acc))
                       nil (set-table set)))

    (define (set-filter predicate set-value)
      (set-fold
       (lambda (element result)
         (if (predicate element) (set-adjoin! result element) result))
       (set (set-key-comparator set-value))
       set-value))

    (define set-filter! set-filter)
    (define (set-remove predicate set-value)
      (set-filter (lambda (element) (not (predicate element))) set-value))
    (define set-remove! set-remove)

    (define (set-partition predicate set)
      (values (set-filter predicate set) (set-remove predicate set)))
    (define set-partition! set-partition)

    (define (set-copy set)
      (make-set (set-copy-table (set-table set) (set-key-comparator set))
                (set-key-comparator set)))

    (define (set->list set) (hash-table-keys (set-table set)))

    (define (list->set comparator elements)
      (fold (lambda (element result) (set-adjoin! result element))
            (set comparator) elements))

    (define (list->set! set elements)
      (fold (lambda (element result) (set-adjoin! result element))
            set elements))

    (define (comparable-sets? left right)
      (if (eq? (set-key-comparator left) (set-key-comparator right))
          #t
          (error "can't compare sets with different comparators" left right)))

    (define (set=? set1 . sets)
      (or (null? sets)
          (and (comparable-sets? set1 (car sets))
               (= (set-size set1) (set-size (car sets)))
               (set-every? (lambda (element) (set-contains? set1 element))
                           (car sets))
               (apply set=? sets))))

    (define (set<? set1 . sets)
      (or (null? sets)
          (and (comparable-sets? set1 (car sets))
               (< (set-size set1) (set-size (car sets)))
               (set-every? (lambda (element) (set-contains? (car sets) element))
                           set1)
               (apply set<? sets))))

    (define (set>? . sets) (apply set<? (reverse sets)))
    (define (set<=? set1 . sets)
      (or (null? sets)
          (and (comparable-sets? set1 (car sets))
               (<= (set-size set1) (set-size (car sets)))
               (set-every? (lambda (element) (set-contains? (car sets) element))
                           set1)
               (apply set<=? sets))))
    (define (set>=? . sets) (apply set<=? (reverse sets)))

    (define (set-union set1 . sets)
      (apply set-union! (set-copy set1) sets))
    (define (set-intersection set1 . sets)
      (apply set-intersection! (set-copy set1) sets))
    (define (set-difference set1 . sets)
      (apply set-difference! (set-copy set1) sets))
    (define (set-xor set1 set2) (set-xor! (set-copy set1) set2))

    (define (set-union! set1 . sets)
      (if (null? sets)
          set1
          (and (comparable-sets? set1 (car sets))
               (apply set-union!
                      (set-fold (lambda (element set)
                                  (set-adjoin! set element))
                                set1 (car sets))
                      (cdr sets)))))

    (define (set-intersection! set1 . sets)
      (if (null? sets)
          set1
          (and (comparable-sets? set1 (car sets))
               (apply set-intersection!
                      (set-filter!
                       (lambda (element) (set-contains? (car sets) element))
                       set1)
                      (cdr sets)))))

    (define (set-difference! set1 . sets)
      (if (null? sets)
          set1
          (and (comparable-sets? set1 (car sets))
               (apply set-difference!
                      (set-remove!
                       (lambda (element) (set-contains? (car sets) element))
                       set1)
                      (cdr sets)))))

    (define (set-xor! set1 set2)
      (and (comparable-sets? set1 set2)
           (set-union
            (set-remove (lambda (element) (set-contains? set2 element)) set1)
            (set-remove (lambda (element) (set-contains? set1 element)) set2))))

    (define set-comparator
      (make-comparator set? set=? set<?
                       (lambda (value . rest)
                         (default-hash (set-size value)
                                       (if (pair? rest) (car rest) (hash-bound)))))))
)
