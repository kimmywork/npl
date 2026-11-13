; SRFI 113 bags with the canonical NPL library name.
;
; A bag is a mutable hash table mapping each element to its multiplicity.
(define-library (npl data bag)
  (export
    bag bag-contains? bag-unfold
    bag? bag-empty? bag-disjoint?
    bag-member bag-element-comparator
    bag-adjoin bag-adjoin! bag-replace bag-replace!
    bag-delete bag-delete! bag-delete-all bag-delete-all!
    bag-search!
    bag-size bag-find bag-count bag-any? bag-every?
    bag-map bag-for-each bag-fold bag-filter bag-filter!
    bag-remove bag-remove! bag-partition bag-partition!
    bag-copy bag->list list->bag list->bag!
    bag=? bag<? bag>? bag<=? bag>=?
    bag-union bag-intersection bag-difference bag-xor
    bag-union! bag-intersection! bag-difference! bag-xor!
    bag-comparator
    bag-unique-size bag-sum bag-sum! bag-product bag-product!
    bag-element-count bag-for-each-unique bag-fold-unique
    bag-increment! bag-decrement! bag->set set->bag set->bag!
    bag->alist alist->bag)
  (import (scheme base) (npl list) (npl comparator) (npl hashtable)
          (npl data set))
  (begin
    (define-record-type bag-type
      (make-bag table comparator)
      bag?
      (table bag-table)
      (comparator bag-key-comparator))

    (define (bag-table-new comparator)
      (make-hash-table
       (comparator-equality-predicate comparator)
       (lambda (element) (comparator-hash comparator element))))

    (define (bag-copy-table table comparator)
      (let ((result (bag-table-new comparator)))
        (hash-table-for-each
         (lambda (key value) (hash-table-set! result key value))
         table)
        result))

    (define (bag comparator . elements)
      (let ((result (make-bag (bag-table-new comparator) comparator)))
        (for-each (lambda (element) (bag-adjoin! result element)) elements)
        result))

    (define (bag-unfold comparator stop? mapper successor seed)
      (let loop ((seed seed) (result (bag comparator)))
        (if (stop? seed)
            result
            (loop (successor seed) (bag-adjoin! result (mapper seed))))))

    (define (bag-contains? bag element)
      (hash-table-contains? (bag-table bag) element))

    (define (bag-empty? bag) (zero? (bag-size bag)))

    (define (bag-disjoint? left right)
      (if (< (hash-table-size (bag-table right))
             (hash-table-size (bag-table left)))
          (bag-disjoint? right left)
          (let ((table (bag-table right)))
            (not (hash-table-find
                  (lambda (key value) (hash-table-contains? table key))
                  (bag-table left)
                  (lambda () #f))))))

    (define (stored-element bag element)
      (hash-table-find (lambda (key value)
                         (if ((comparator-equality-predicate
                               (bag-key-comparator bag))
                              key element)
                             key
                             #f))
                       (bag-table bag)
                       (lambda () #f)))

    (define (bag-member bag element default)
      (let ((stored (stored-element bag element)))
        (if stored stored default)))

    (define (bag-element-comparator bag) (bag-key-comparator bag))

    (define (bag-adjoin bag . elements)
      (apply bag-adjoin! (bag-copy bag) elements))

    (define (bag-adjoin! bag . elements)
      (for-each
       (lambda (element)
         (hash-table-update!/default (bag-table bag) element
                                     (lambda (count) (+ count 1))
                                     0))
       elements)
      bag)

    (define (bag-replace bag element)
      (bag-replace! (bag-copy bag) element))

    (define (bag-replace! bag element)
      (let ((count (hash-table-ref/default (bag-table bag) element 0)))
        (if (positive? count)
            (begin
              (hash-table-delete! (bag-table bag) element)
              (hash-table-set! (bag-table bag) element count))))
      bag)

    (define (bag-delete bag . elements) (bag-delete-all bag elements))
    (define (bag-delete! bag . elements) (bag-delete-all! bag elements))

    (define (bag-delete-all bag elements)
      (bag-delete-all! (bag-copy bag) elements))

    (define (bag-delete-all! bag elements)
      (let ((table (bag-table bag)))
        (for-each
         (lambda (element)
           (let ((count (- (hash-table-ref/default table element 0) 1)))
             (cond ((positive? count)
                    (hash-table-set! table element count))
                   ((zero? count)
                    (hash-table-delete! table element)))))
         elements))
      bag)

    (define bag-search!
      (let ((not-found (list 'not-found)))
        (lambda (bag element failure success)
          (let ((found (hash-table-ref/default (bag-table bag)
                                               element not-found)))
            (if (eq? found not-found)
                (failure
                 (lambda (object)
                   (hash-table-set! (bag-table bag) element 1)
                   (values bag object))
                 (lambda (object) (values bag object)))
                (success
                 (stored-element bag element)
                 (lambda (new-element object)
                   (hash-table-delete! (bag-table bag) element)
                   (bag-adjoin! bag new-element)
                   (values bag object))
                 (lambda (object)
                   (hash-table-delete! (bag-table bag) element)
                   (values bag object))))))))

    (define (bag-size bag)
      (hash-table-fold (bag-table bag)
                       (lambda (element count total) (+ count total))
                       0))

    (define (bag-find predicate bag failure)
      (call-with-current-continuation
       (lambda (return)
         (hash-table-for-each
          (lambda (element count)
            (if (predicate element) (return element)))
          (bag-table bag))
         (failure))))

    (define (bag-count predicate bag)
      (hash-table-fold
       (lambda (element count total)
         (+ total (if (predicate element) count 0)))
       0 (bag-table bag)))

    (define (bag-any? predicate bag)
      (and (hash-table-find (lambda (key value) (predicate key))
                            (bag-table bag)
                            (lambda () #f))
           #t))

    (define (bag-every? predicate bag)
      (not (bag-any? (lambda (element) (not (predicate element))) bag)))

    (define (bag-map comparator proc bag-value)
      (bag-fold (lambda (element result) (bag-adjoin! result (proc element)))
                (bag comparator)
                bag-value))

    (define (bag-for-each proc bag)
      (hash-table-for-each
       (lambda (element count)
         (let loop ((i count))
           (if (positive? i)
               (begin (proc element) (loop (- i 1))))))
       (bag-table bag)))

    (define (bag-fold proc nil bag)
      (hash-table-fold
       (lambda (element count result)
         (let loop ((i count) (result result))
           (if (zero? i) result (loop (- i 1) (proc element result)))))
       nil (bag-table bag)))

    (define (bag-filter predicate bag-value)
      (bag-fold
       (lambda (element result)
         (if (predicate element) (bag-adjoin! result element) result))
       (bag (bag-key-comparator bag-value))
       bag-value))

    (define bag-filter! bag-filter)
    (define (bag-remove predicate bag-value)
      (bag-filter (lambda (element) (not (predicate element))) bag-value))
    (define bag-remove! bag-remove)
    (define (bag-partition predicate bag)
      (values (bag-filter predicate bag) (bag-remove predicate bag)))
    (define bag-partition! bag-partition)

    (define (bag-copy bag)
      (make-bag (bag-copy-table (bag-table bag) (bag-key-comparator bag))
                (bag-key-comparator bag)))

    (define (bag->list bag)
      (let loop ((entries (hash-table->alist (bag-table bag))) (result '()))
        (if (null? entries)
            (reverse result)
            (let ((element (car (car entries)))
                  (count (cdr (car entries))))
              (loop (cdr entries)
                    (let repeat ((remaining count) (result result))
                      (if (zero? remaining)
                          result
                          (repeat (- remaining 1)
                                  (cons element result)))))))))

    (define (list->bag comparator elements)
      (fold (lambda (element result) (bag-adjoin! result element))
            (bag comparator) elements))

    (define (list->bag! bag elements)
      (fold (lambda (element result) (bag-adjoin! result element))
            bag elements))

    (define (comparable-bags? left right)
      (if (eq? (bag-key-comparator left) (bag-key-comparator right))
          #t
          (error "can't compare bags with different comparators" left right)))

    (define (bag-subset? left right strict)
      (and (comparable-bags? left right)
           (let ((left-table (bag-table left))
                 (right-table (bag-table right)))
             (and (hash-table-fold
                   left-table
                   (lambda (element count result)
                     (and result
                          (let ((other
                                 (hash-table-ref/default right-table
                                                         element 0)))
                            (if strict
                                (<= count other)
                                (<= count other)))))
                   #t)
                  (if strict
                      (< (bag-size left) (bag-size right))
                      #t)))))

    (define (bag=? bag1 . bags)
      (or (null? bags)
          (and (comparable-bags? bag1 (car bags))
               (= (bag-size bag1) (bag-size (car bags)))
               (bag-subset? bag1 (car bags) #f)
               (bag-subset? (car bags) bag1 #f)
               (apply bag=? bags))))

    (define (bag<? bag1 . bags)
      (or (null? bags)
          (and (bag-subset? bag1 (car bags) #t)
               (apply bag<? bags))))

    (define (bag>? . bags) (apply bag<? (reverse bags)))
    (define (bag<=? bag1 . bags)
      (or (null? bags)
          (and (bag-subset? bag1 (car bags) #f)
               (apply bag<=? bags))))
    (define (bag>=? . bags) (apply bag<=? (reverse bags)))

    (define (bag-union bag1 . bags)
      (apply bag-union! (bag-copy bag1) bags))
    (define (bag-intersection bag1 . bags)
      (apply bag-intersection! (bag-copy bag1) bags))
    (define (bag-difference bag1 . bags)
      (apply bag-difference! (bag-copy bag1) bags))
    (define (bag-xor bag1 bag2) (bag-xor! (bag-copy bag1) bag2))

    (define (bag-union! bag1 . bags)
      (if (null? bags)
          bag1
          (and (comparable-bags? bag1 (car bags))
               (begin
                 (hash-table-for-each
                  (lambda (element count)
                    (hash-table-update!/default
                     (bag-table bag1) element
                     (lambda (old) (max old count)) count))
                  (bag-table (car bags)))
                 (apply bag-union! bag1 (cdr bags))))))

    (define (bag-intersection! bag1 . bags)
      (if (null? bags)
          bag1
          (and (comparable-bags? bag1 (car bags))
               (let ((other (bag-table (car bags))))
                 (hash-table-for-each
                  (lambda (element count)
                    (let ((count2 (min count
                                       (hash-table-ref/default
                                        other element 0))))
                      (if (positive? count2)
                          (hash-table-set! (bag-table bag1) element count2)
                          (hash-table-delete! (bag-table bag1) element))))
                  (bag-table bag1))
                 (apply bag-intersection! bag1 (cdr bags))))))

    (define (bag-difference! bag1 . bags)
      (if (null? bags)
          bag1
          (and (comparable-bags? bag1 (car bags))
               (let ((other (bag-table (car bags))))
                 (hash-table-for-each
                  (lambda (element count)
                    (let ((count2 (- count
                                     (hash-table-ref/default
                                      other element 0))))
                      (if (positive? count2)
                          (hash-table-set! (bag-table bag1) element count2)
                          (hash-table-delete! (bag-table bag1) element))))
                  (bag-table bag1))
                 (apply bag-difference! bag1 (cdr bags))))))

    (define (bag-xor! bag1 bag2)
      (and (comparable-bags? bag1 bag2)
           (let ((table1 (bag-table bag1))
                 (table2 (bag-table bag2)))
             (hash-table-for-each
              (lambda (element count)
                (let ((count2 (abs (- count
                                      (hash-table-ref/default
                                       table1 element 0)))))
                  (if (positive? count2)
                      (hash-table-set! table1 element count2)
                      (hash-table-delete! table1 element))))
              table2)
             bag1)))

    (define (bag-sum bag1 . bags)
      (apply bag-sum! (bag-copy bag1) bags))

    (define (bag-sum! bag1 . bags)
      (if (null? bags)
          bag1
          (and (comparable-bags? bag1 (car bags))
               (begin
                 (hash-table-for-each
                  (lambda (element count)
                    (hash-table-update!/default
                     (bag-table bag1) element
                     (lambda (old) (+ old count)) 0))
                  (bag-table (car bags)))
                 (apply bag-sum! bag1 (cdr bags))))))

    (define (bag-product count bag)
      (bag-product! count (bag-copy bag)))

    (define (bag-product! count bag)
      (let ((table (bag-table bag)))
        (for-each
         (lambda (element)
           (let ((product (* count (hash-table-ref/default table element 0))))
             (if (positive? product)
                 (hash-table-set! table element product)
                 (hash-table-delete! table element))))
         (hash-table-keys table)))
      bag)

    (define (bag-unique-size bag) (hash-table-size (bag-table bag)))
    (define (bag-element-count bag element)
      (hash-table-ref/default (bag-table bag) element 0))
    (define (bag-for-each-unique proc bag)
      (hash-table-for-each proc (bag-table bag)))
    (define (bag-fold-unique proc nil bag)
      (hash-table-fold proc nil (bag-table bag)))

    (define (bag-increment! bag element count)
      (let* ((table (bag-table bag))
             (count2 (+ count (hash-table-ref/default table element 0))))
        (if (positive? count2)
            (hash-table-set! table element count2)
            (hash-table-delete! table element))
        bag))
    (define (bag-decrement! bag element count)
      (bag-increment! bag element (- count)))

    (define (bag->set bag)
      (let ((result (set (bag-key-comparator bag))))
        (hash-table-for-each
         (lambda (element count) (set-adjoin! result element))
         (bag-table bag))
        result))

    (define (set->bag set-value)
      (set->bag! (bag (set-element-comparator set-value)) set-value))
    (define (set->bag! bag set-value)
      (set-for-each (lambda (element) (bag-adjoin! bag element)) set-value)
      bag)

    (define (bag->alist bag) (hash-table->alist (bag-table bag)))
    (define (alist->bag comparator alist)
      (let ((result (bag comparator)))
        (for-each (lambda (entry)
                    (bag-increment! result (car entry) (cdr entry)))
                  alist)
        result))

    (define bag-comparator
      (make-comparator bag? bag=? #f
                       (lambda (value)
                         (default-hash (bag->alist value)))) ))
)
