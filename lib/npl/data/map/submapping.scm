;; Submappings

(define mapping=?
  (case-lambda
    ((comparator mapping)
     (assume (mapping? mapping))
     #t)
    ((comparator mapping1 mapping2) (%mapping=? comparator mapping1 mapping2))
    ((comparator mapping1 mapping2 . mappings)
     (and (%mapping=? comparator mapping1 mapping2)
          (apply mapping=? comparator mapping2 mappings)))))
(define (%mapping=? comparator mapping1 mapping2)
  (and (eq? (mapping-key-comparator mapping1) (mapping-key-comparator mapping2))
       (%mapping<=? comparator mapping1 mapping2)
       (%mapping<=? comparator mapping2 mapping1)))

(define mapping<=?
  (case-lambda
    ((comparator mapping)
     (assume (mapping? mapping))
     #t)
    ((comparator mapping1 mapping2)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping<=? comparator mapping1 mapping2))
    ((comparator mapping1 mapping2 . mappings)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (and (%mapping<=? comparator mapping1 mapping2)
          (apply mapping<=? comparator mapping2 mappings)))))

(define (%mapping<=? comparator mapping1 mapping2)
  (assume (comparator? comparator))
  (assume (mapping? mapping1))
  (assume (mapping? mapping2))
  (let ((less? (comparator-ordering-predicate (mapping-key-comparator mapping1)))
    (equality-predicate (comparator-equality-predicate comparator))
    (gen1 (tree-generator (mapping-tree mapping1)))
    (gen2 (tree-generator (mapping-tree mapping2))))    
    (let loop ((item1 (gen1))
           (item2 (gen2)))
      (cond
       ((eof-object? item1)
    #t)
       ((eof-object? item2)
    #f)
       (else
    (let ((key1 (car item1)) (value1 (cadr item1))
          (key2 (car item2)) (value2 (cadr item2)))
      (cond
       ((less? key1 key2)
        #f)
       ((less? key2 key1)
        (loop item1 (gen2)))
       ((equality-predicate value1 value2)
        (loop (gen1) (gen2)))
       (else
        #f))))))))

(define mapping>?
  (case-lambda
    ((comparator mapping)
     (assume (mapping? mapping))
     #t)
    ((comparator mapping1 mapping2)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping>? comparator mapping1 mapping2))
    ((comparator mapping1 mapping2 . mappings)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (and (%mapping>? comparator  mapping1 mapping2)
          (apply mapping>? comparator mapping2 mappings)))))

(define (%mapping>? comparator mapping1 mapping2)
  (assume (comparator? comparator))
  (assume (mapping? mapping1))
  (assume (mapping? mapping2))
  (and (%mapping<=? comparator mapping2 mapping1)
       (not (%mapping<=? comparator mapping1 mapping2))))

(define mapping<?
  (case-lambda
    ((comparator mapping)
     (assume (mapping? mapping))
     #t)
    ((comparator mapping1 mapping2)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping<? comparator mapping1 mapping2))
    ((comparator mapping1 mapping2 . mappings)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (and (%mapping<? comparator  mapping1 mapping2)
          (apply mapping<? comparator mapping2 mappings)))))

(define (%mapping<? comparator mapping1 mapping2)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping>? comparator mapping2 mapping1))

(define mapping>=?
  (case-lambda
    ((comparator mapping)
     (assume (mapping? mapping))
     #t)
    ((comparator mapping1 mapping2)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping>=? comparator mapping1 mapping2))
    ((comparator mapping1 mapping2 . mappings)
     (assume (comparator? comparator))
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (and (%mapping>=? comparator mapping1 mapping2)
          (apply mapping>=? comparator mapping2 mappings)))))

(define (%mapping>=? comparator mapping1 mapping2)
  (assume (comparator? comparator))
  (assume (mapping? mapping1))
  (assume (mapping? mapping2))
  (and (%mapping<=? comparator mapping2 mapping1)
       (not (%mapping<=? comparator mapping1 mapping2))))

;; Set theory operations

(define (%mapping-union mapping1 mapping2)
  (mapping-fold (lambda (key2 value2 mapping)
          (receive (mapping obj)
              (mapping-search mapping
                      key2
                      (lambda (insert ignore)
                    (insert value2 #f))
                      (lambda (key1 value1 update remove)
                    (update key1 value1 #f)))
            mapping))
        mapping1 mapping2))

(define (%mapping-intersection mapping1 mapping2)
  (mapping-filter (lambda (key1 value1)
        (mapping-contains? mapping2 key1))
          mapping1))

(define (%mapping-difference mapping1 mapping2)
  (mapping-fold (lambda (key2 value2 mapping)
          (receive (mapping obj)
          (mapping-search mapping
                  key2
                  (lambda (insert ignore)
                (ignore #f))
                  (lambda (key1 value1 update remove)
                (remove #f)))
        mapping))
        mapping1 mapping2))

(define (%mapping-xor mapping1 mapping2)
  (mapping-fold (lambda (key2 value2 mapping)
          (receive (mapping obj)
          (mapping-search mapping
                  key2
                  (lambda (insert ignore)
                (insert value2 #f))
                  (lambda (key1 value1 update remove)
                (remove #f)))
        mapping))
        mapping1 mapping2))

(define mapping-union
  (case-lambda
    ((mapping)
     (assume (mapping? mapping))
     mapping)
    ((mapping1 mapping2)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping-union mapping1 mapping2))
    ((mapping1 mapping2 . mappings)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (apply mapping-union (%mapping-union mapping1 mapping2) mappings))))
(define (mapping-union! . args) (apply mapping-union args))

(define mapping-intersection
  (case-lambda
    ((mapping)
     (assume (mapping? mapping))
     mapping)
    ((mapping1 mapping2)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping-intersection mapping1 mapping2))
    ((mapping1 mapping2 . mappings)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (apply mapping-intersection (%mapping-intersection mapping1 mapping2) mappings))))
(define (mapping-intersection! . args) (apply mapping-intersection args))

(define mapping-difference
  (case-lambda
    ((mapping)
     (assume (mapping? mapping))
     mapping)
    ((mapping1 mapping2)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping-difference mapping1 mapping2))
    ((mapping1 mapping2 . mappings)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (apply mapping-difference (%mapping-difference mapping1 mapping2) mappings))))
(define (mapping-difference! . args) (apply mapping-difference args))

(define mapping-xor
  (case-lambda
    ((mapping)
     (assume (mapping? mapping))
     mapping)
    ((mapping1 mapping2)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (%mapping-xor mapping1 mapping2))
    ((mapping1 mapping2 . mappings)
     (assume (mapping? mapping1))
     (assume (mapping? mapping2))
     (apply mapping-xor (%mapping-xor mapping1 mapping2) mappings))))
(define (mapping-xor! . args) (apply mapping-xor args))

;; Additional procedures for mappings with ordererd keys

(define (mapping-min-key mapping)
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-fold (lambda (key value acc)
             (return key))
           #f mapping)
     (error "mapping-min-key: empty map"))))

(define (mapping-max-key mapping)
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-fold/reverse (lambda (key value acc)
                 (return key))
               #f mapping)
     (error "mapping-max-key: empty map"))))

(define (mapping-min-value mapping)
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-fold (lambda (key value acc)
             (return value))
           #f mapping)
     (error "mapping-min-value: empty map"))))

(define (mapping-max-value mapping)
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-fold/reverse (lambda (key value acc)
                 (return value))
               #f mapping)
     (error "mapping-max-value: empty map"))))

(define (mapping-min-entry mapping)
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-fold (lambda (key value acc)
             (return key value))
           #f mapping)
     (error "mapping-min-entry: empty map"))))

(define (mapping-max-entry mapping)
  (assume (mapping? mapping))
  (call/cc
   (lambda (return)
     (mapping-fold/reverse (lambda (key value acc)
                 (return key value))
               #f mapping)
     (error "mapping-max-entry: empty map"))))

(define (mapping-key-predecessor mapping obj failure)
  (assume (mapping? mapping))
  (assume (procedure? failure))
  (tree-key-predecessor (mapping-key-comparator mapping) (mapping-tree mapping) obj failure))

(define (mapping-key-successor mapping obj failure)
  (assume (mapping? mapping))
  (assume (procedure? failure))
  (tree-key-successor (mapping-key-comparator mapping) (mapping-tree mapping) obj failure))

(define (mapping-range= mapping obj)
  (assume (mapping? mapping))
  (let ((comparator (mapping-key-comparator mapping)))
    (receive (tree< tree<= tree= tree>= tree>)
    (tree-split comparator (mapping-tree mapping) obj)
      (%make-mapping comparator tree=))))

(define (mapping-range< mapping obj)
  (assume (mapping? mapping))
  (let ((comparator (mapping-key-comparator mapping)))
    (receive (tree< tree<= tree= tree>= tree>)
    (tree-split comparator (mapping-tree mapping) obj)
      (%make-mapping comparator tree<))))

(define (mapping-range<= mapping obj)
  (assume (mapping? mapping))
  (let ((comparator (mapping-key-comparator mapping)))
    (receive (tree< tree<= tree= tree>= tree>)
    (tree-split comparator (mapping-tree mapping) obj)
      (%make-mapping comparator tree<=))))

(define (mapping-range> mapping obj)
  (assume (mapping? mapping))
  (let ((comparator (mapping-key-comparator mapping)))
    (receive (tree< tree<= tree= tree>= tree>)
    (tree-split comparator (mapping-tree mapping) obj)
      (%make-mapping comparator tree>))))

(define (mapping-range>= mapping obj)
  (assume (mapping? mapping))
  (assume (mapping? mapping))
  (let ((comparator (mapping-key-comparator mapping)))
    (receive (tree< tree<= tree= tree>= tree>)
    (tree-split comparator (mapping-tree mapping) obj)
      (%make-mapping comparator tree>=))))

(define (mapping-range=! . args) (apply mapping-range= args))
(define (mapping-range<! . args) (apply mapping-range< args))
(define (mapping-range>! . args) (apply mapping-range> args))
(define (mapping-range<=! . args) (apply mapping-range<= args))
(define (mapping-range>=! . args) (apply mapping-range>= args))

(define (mapping-split mapping obj)
  (assume (mapping? mapping))
  (let ((comparator (mapping-key-comparator mapping)))
    (receive (tree< tree<= tree= tree>= tree>)
    (tree-split comparator (mapping-tree mapping) obj)
      (values (%make-mapping comparator tree<)
          (%make-mapping comparator tree<=)
          (%make-mapping comparator tree=)
          (%make-mapping comparator tree>=)
          (%make-mapping comparator tree>)))))

(define (mapping-catenate comparator mapping1 pivot-key pivot-value mapping2)
  (assume (comparator? comparator))
  (assume (mapping? mapping1))
  (assume (mapping? mapping2))
  (%make-mapping comparator (tree-catenate (mapping-tree mapping1)
                       pivot-key
                       pivot-value
                       (mapping-tree mapping2))))

(define (mapping-catenate! . args) (apply mapping-catenate args))

(define (mapping-map/monotone proc comparator mapping)
  (assume (procedure? proc))
  (assume (comparator? comparator))
  (assume (mapping? mapping))
  (%make-mapping comparator (tree-map proc (mapping-tree mapping))))

(define (mapping-map/monotone! . args) (apply mapping-map/monotone args))

(define (mapping-fold/reverse proc acc mapping)
  (assume (procedure? proc))
  (assume (mapping? mapping))
  (tree-fold/reverse proc acc (mapping-tree mapping)))

;; Comparators

(define (mapping-equality comparator)
  (assume (comparator? comparator))
  (lambda (mapping1 mapping2)
    (mapping=? comparator mapping1 mapping2)))

(define (mapping-ordering comparator)
  (assume (comparator? comparator))
  (let ((value-equality (comparator-equality-predicate comparator))
    (value-ordering (comparator-ordering-predicate comparator)))
    (lambda (mapping1 mapping2)
      (let* ((key-comparator (mapping-key-comparator mapping1))
         (equality (comparator-equality-predicate key-comparator))
         (ordering (comparator-ordering-predicate key-comparator))
         (gen1 (tree-generator (mapping-tree mapping1)))
         (gen2 (tree-generator (mapping-tree mapping2))))
    (let loop ()
      (let ((item1 (gen1)) (item2 (gen2)))
        (cond
         ((eof-object? item1)
          (not (eof-object? item2)))
         ((eof-object? item2)
          #f)
         (else
          (let ((key1 (car item1)) (value1 (cadr item1))
            (key2 (car item2)) (value2 (cadr item2)))
        (cond
         ((equality key1 key2)
          (if (value-equality value1 value2)
              (loop)
              (value-ordering value1 value2)))
         (else
          (ordering key1 key2))))))))))))

(define (make-mapping-comparator comparator)
  (make-comparator mapping? (mapping-equality comparator) (mapping-ordering comparator) #f))

(define mapping-comparator (make-mapping-comparator (make-default-comparator)))

(comparator-register-default! mapping-comparator)

(define (mapping-split! mapping obj) (mapping-split mapping obj))
