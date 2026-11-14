;; Copyright (C) Marc Nieper-Wißkirchen (2016, 2017).  All Rights
;; Reserved.

;; Permission is hereby granted, free of charge, to any person
;; obtaining a copy of this software and associated documentation
;; files (the "Software"), to deal in the Software without
;; restriction, including without limitation the rights to use, copy,
;; modify, merge, publish, distribute, sublicense, and/or sell copies
;; of the Software, and to permit persons to whom the Software is
;; furnished to do so, subject to the following conditions:

;; The above copyright notice and this permission notice shall be
;; included in all copies or substantial portions of the Software.

;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
;; MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
;; BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
;; ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
;; CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
;; SOFTWARE.

;;; New types

(define-record-type <mapping>
  (%make-mapping comparator tree)
  mapping?
  (comparator mapping-key-comparator)
  (tree mapping-tree))

(define (make-empty-mapping comparator)
  (assume (comparator? comparator))
  (%make-mapping comparator (make-tree)))

;;; Exported procedures

;; Constructors

(define (mapping comparator . args)
  (assume (comparator? comparator))
  (mapping-unfold null?
          (lambda (args)
        (values (car args)
            (cadr args)))
          cddr
          args
          comparator))

(define (mapping-unfold stop? mapper successor seed comparator)
  (assume (procedure? stop?))
  (assume (procedure? mapper))
  (assume (procedure? successor))
  (assume (comparator? comparator))
  (let loop ((mapping (make-empty-mapping comparator))
         (seed seed))
    (if (stop? seed)
    mapping
    (receive (key value)
        (mapper seed)
      (loop (mapping-adjoin mapping key value)
        (successor seed))))))

(define (mapping/ordered . args) (apply mapping args))
(define (mapping-unfold/ordered . args) (apply mapping-unfold args))

;; Predicates

(define (mapping-empty? mapping)
  (assume (mapping? mapping))
  (not (mapping-any? (lambda (key value) #t) mapping)))

(define (mapping-contains? mapping key)
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-search mapping
         key
         (lambda (insert ignore)
           (return #f))
         (lambda (key value update remove)
           (return #t))))))

(define (mapping-disjoint? mapping1 mapping2)
  (assume (mapping? mapping1))
  (assume (mapping? mapping2))
  (call/cc
   (lambda (return)
     (mapping-for-each (lambda (key value)
             (when (mapping-contains? mapping2 key)
               (return #f)))
           mapping1)
     #t)))

;; Accessors

(define mapping-ref
  (case-lambda
    ((mapping key)
     (assume (mapping? mapping))
     (mapping-ref mapping key (lambda ()
            (error "mapping-ref: key not in mapping" key))))
    ((mapping key failure)
     (assume (mapping? mapping))
     (assume (procedure? failure))
     (mapping-ref mapping key failure (lambda (value)
                value)))
    ((mapping key failure success)
     (assume (mapping? mapping))
     (assume (procedure? failure))
     (assume (procedure? success))
     ((call/cc
       (lambda (return-thunk)
     (mapping-search mapping
             key
             (lambda (insert ignore)
               (return-thunk failure))
             (lambda (key value update remove)
               (return-thunk (lambda () (success value)))))))))))

(define (mapping-ref/default mapping key default)
  (assume (mapping? mapping))
  (mapping-ref mapping key (lambda () default)))

;; Updaters

(define (mapping-adjoin mapping . args)
  (assume (mapping? mapping))
  (let loop ((args args)
         (mapping mapping))
    (if (null? args)
    mapping
    (receive (mapping value)
        (mapping-intern mapping (car args) (lambda () (cadr args)))
      (loop (cddr args) mapping)))))

(define (mapping-adjoin! . args) (apply mapping-adjoin args))

(define (mapping-set mapping . args)
  (assume (mapping? mapping))
  (let loop ((args args)
         (mapping mapping))
    (if (null? args)
    mapping
    (receive (mapping)
        (mapping-update mapping (car args) (lambda (value) (cadr args)) (lambda () #f))    
      (loop (cddr args)
        mapping)))))

(define (mapping-set! . args) (apply mapping-set args))

(define (mapping-replace mapping key value)
  (assume (mapping? mapping))
  (receive (mapping obj)
      (mapping-search mapping
          key
          (lambda (insert ignore)
            (ignore #f))
          (lambda (old-key old-value update remove)
            (update key value #f)))
    mapping))

(define (mapping-replace! . args) (apply mapping-replace args))

(define (mapping-delete mapping . keys)
  (assume (mapping? mapping))
  (mapping-delete-all mapping keys))

(define (mapping-delete! . args) (apply mapping-delete args))

(define (mapping-delete-all mapping keys)
  (assume (mapping? mapping))
  (assume (list? keys))
  (fold (lambda (key mapping)
      (receive (mapping obj)
          (mapping-search mapping
              key
              (lambda (insert ignore)
                (ignore #f))
              (lambda (old-key old-value update remove)
                (remove #f)))
        mapping))
    mapping keys))

(define (mapping-delete-all! . args) (apply mapping-delete-all args))

(define (mapping-intern mapping key failure)
  (assume (mapping? mapping))
  (assume (procedure? failure))
  (call/cc
   (lambda (return)
     (mapping-search mapping
         key
         (lambda (insert ignore)
           (receive (value)
               (failure)
             (insert value value)))
         (lambda (old-key old-value update remove)
           (return mapping old-value))))))

(define (mapping-intern! . args) (apply mapping-intern args))

(define mapping-update 
  (case-lambda
   ((mapping key updater)
    (mapping-update mapping key updater (lambda ()
                  (error "mapping-update: key not found in mapping" key))))
   ((mapping key updater failure)
    (mapping-update mapping key updater failure (lambda (value)
                      value)))
   ((mapping key updater failure success)
    (assume (mapping? mapping))
    (assume (procedure? updater))
    (assume (procedure? failure))
    (assume (procedure? success))
    (receive (mapping obj)
    (mapping-search mapping
            key
            (lambda (insert ignore)
              (insert (updater (failure)) #f))
            (lambda (old-key old-value update remove)
              (update key (updater (success old-value)) #f)))
      mapping))))

(define (mapping-update! . args) (apply mapping-update args))

(define (mapping-update/default mapping key updater default)
  (mapping-update mapping key updater (lambda () default)))

(define (mapping-update!/default . args) (apply mapping-update/default args))

(define mapping-pop
  (case-lambda
    ((mapping)
     (mapping-pop mapping (lambda ()
                (error "mapping-pop: mapping has no association"))))
    ((mapping failure)
     (assume (mapping? mapping))
     (assume (procedure? failure))
     ((call/cc
       (lambda (return-thunk)
     (receive (key value)
         (mapping-find (lambda (key value) #t) mapping (lambda () (return-thunk failure)))
       (lambda ()
         (values (mapping-delete mapping key) key value)))))))))

(define (mapping-pop! . args) (apply mapping-pop args))

(define (mapping-search mapping key failure success)
  (assume (mapping? mapping))
  (assume (procedure? failure))
  (assume (procedure? success))
  (let*-values
      (((comparator)
        (mapping-key-comparator mapping))
       ((tree obj)
        (tree-search comparator
                     (mapping-tree mapping)
                     key
                     (lambda (insert ignore)
                       (failure (lambda (value obj)
                                  (insert key value obj))
                                ignore))
                     (lambda (old-key old-value update remove)
                       (success old-key old-value update remove)))))
    (values (%make-mapping comparator tree) obj)))

(define (mapping-search! mapping key failure success)
  (mapping-search mapping key failure success))

;; The whole mapping

(define (mapping-size mapping)
  (assume (mapping? mapping))
  (mapping-count (lambda (key value)
           #t)
         mapping))

(define (mapping-find predicate mapping failure)
  (assume (procedure? predicate))
  (assume (mapping? mapping))
  (assume (procedure? failure))
  (call/cc
   (lambda (return)
     (mapping-for-each (lambda (key value)
             (when (predicate key value)
               (return key value)))
           mapping)
     (failure))))

(define (mapping-count predicate mapping)
  (assume (procedure? predicate))
  (assume (mapping? mapping))
  (mapping-fold (lambda (key value count)
          (if (predicate key value)
          (+ 1 count)
          count))
        0 mapping))

(define (mapping-any? predicate mapping)
  (assume (procedure? predicate))
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-for-each (lambda (key value)
             (when (predicate key value)
               (return #t)))
           mapping)
     #f)))

(define (mapping-every? predicate mapping)
  (assume (procedure? predicate))
  (assume (mapping? mapping))
  (not (mapping-any? (lambda (key value)
           (not (predicate key value)))
         mapping)))

(define (mapping-keys mapping)
  (assume (mapping? mapping))
  (mapping-fold/reverse (lambda (key value keys)
              (cons key keys))
            '() mapping))

(define (mapping-values mapping)
  (assume (mapping? mapping))
  (mapping-fold/reverse (lambda (key value values)
              (cons value values))
            '() mapping))

(define (mapping-entries mapping)
  (assume (mapping? mapping))
  (values (mapping-keys mapping)
      (mapping-values mapping)))

;; Mapping and folding

(define (mapping-map proc comparator mapping)
  (assume (procedure? proc))
  (assume (comparator? comparator))
  (assume (mapping? mapping))
  (mapping-fold (lambda (key value mapping)
          (receive (key value)
          (proc key value)
        (mapping-set mapping key value)))
        (make-empty-mapping comparator)
        mapping))

(define (mapping-for-each proc mapping)
  (assume (procedure? proc))
  (assume (mapping? mapping))
  (tree-for-each proc (mapping-tree mapping)))

(define (mapping-fold proc acc mapping)
  (assume (procedure? proc))
  (assume (mapping? mapping))
  (tree-fold proc acc (mapping-tree mapping)))

(define (mapping-map->list proc mapping)
  (assume (procedure? proc))
  (assume (mapping? mapping))
  (reverse (mapping-fold (lambda (key value lst)
                           (cons (proc key value) lst))
                         '()
                         mapping)))

(define (mapping-filter predicate mapping)
  (assume (procedure? predicate))
  (assume (mapping? mapping))
  (mapping-fold (lambda (key value mapping)
          (if (predicate key value)
          (mapping-set mapping key value)
          mapping))
        (make-empty-mapping (mapping-key-comparator mapping))
        mapping))

(define (mapping-filter! . args) (apply mapping-filter args))

(define (mapping-remove predicate mapping)
  (assume (procedure? predicate))
  (assume (mapping? mapping))
  (mapping-filter (lambda (key value)
        (not (predicate key value)))
          mapping))

(define (mapping-remove! . args) (apply mapping-remove args))

(define (mapping-partition predicate mapping)
  (assume (procedure? predicate))
  (assume (mapping? mapping))
  (values (mapping-filter predicate mapping)
      (mapping-remove predicate mapping)))

(define (mapping-partition! . args) (apply mapping-partition args))

;; Copying and conversion

(define (mapping-copy mapping)
  (assume (mapping? mapping))
  mapping)

(define (mapping->alist mapping)
  (assume (mapping? mapping))
  (reverse
   (mapping-fold (lambda (key value alist)
           (cons (cons key value) alist))
         '() mapping)))

(define (alist->mapping comparator alist)
  (assume (comparator? comparator))
  (assume (list? alist))
  (mapping-unfold null?
          (lambda (alist)
        (let ((key (caar alist))
              (value (cdar alist)))
          (values key value)))
          cdr
          alist
          comparator))

(define (alist->mapping! mapping alist)
  (assume (mapping? mapping))
  (assume (list? alist))
  (fold (lambda (association mapping)
      (let ((key (car association))
        (value (cdr association)))
        (if (mapping-contains? mapping key)
            mapping
            (mapping-set mapping key value))))
    mapping
    alist))

(define (alist->mapping/ordered . args) (apply alist->mapping args))
(define (alist->mapping/ordered! . args) (apply alist->mapping! args))

