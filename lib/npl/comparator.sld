; SRFI 128 comparators with the canonical NPL library name.
;
; A comparator bundles a type predicate, an equality predicate, an optional
; ordering predicate, and an optional hash function. SRFI 113 and SRFI 146
; use this library.
(define-library (npl comparator)
  (export
    comparator? comparator-ordered? comparator-hashable?
    make-comparator make-pair-comparator make-list-comparator
    make-vector-comparator make-eq-comparator make-eqv-comparator
    make-equal-comparator
    boolean-hash char-hash char-ci-hash string-hash string-ci-hash
    symbol-hash number-hash hash-bound hash-salt
    make-default-comparator default-hash comparator-register-default!
    comparator-type-test-predicate comparator-equality-predicate
    comparator-ordering-predicate comparator-hash-function
    comparator-test-type comparator-check-type comparator-hash
    =? <? >? <=? >=?
    comparator-if<=>
    comparator-max comparator-min comparator-max-in-list comparator-min-in-list
    default-comparator boolean-comparator real-comparator
    char-comparator char-ci-comparator string-comparator string-ci-comparator
    list-comparator vector-comparator
    eq-comparator eqv-comparator equal-comparator)
  (import (scheme base) (scheme char)
          (only (npl hashtable) string-hash string-ci-hash))
  (begin
    (define-record-type comparator-type
      (make-comparator-record type-test equality ordering hash)
      comparator?
      (type-test comparator-type-test-predicate)
      (equality comparator-equality-predicate)
      (ordering comparator-ordering-predicate)
      (hash comparator-hash-function))

    (define (comparator-ordered? comparator)
      (if (comparator-ordering-predicate comparator) #t #f))
    (define (comparator-hashable? comparator)
      (if (comparator-hash-function comparator) #t #f))

    (define (make-comparator type-test equality ordering . rest)
      (make-comparator-record
       type-test equality ordering
       (if (pair? rest) (car rest) #f)))

    (define (comparator-test-type comparator object)
      ((comparator-type-test-predicate comparator) object))

    (define (comparator-check-type comparator object)
      (if (comparator-test-type comparator object)
          #t
          (error "comparator: type check failed" object)))

    (define hash-limit 536870912)
    (define (hash-bound) hash-limit)
    (define (hash-salt) 16064047)

    (define (number-hash value . rest)
      (let ((bound (if (pair? rest) (car rest) hash-limit)))
        (if (exact-integer? value)
            (modulo (abs value) bound)
            (modulo (abs (exact (round (* value 1000003)))) bound))))

    (define (boolean-hash value . rest)
      (let ((bound (if (pair? rest) (car rest) hash-limit)))
        (if value (modulo 1 bound) 0)))

    (define (char-hash value . rest)
      (let ((bound (if (pair? rest) (car rest) hash-limit)))
        (modulo (char->integer value) bound)))

    (define (char-ci-hash value . rest)
      (let ((bound (if (pair? rest) (car rest) hash-limit)))
        (modulo (char->integer (char-foldcase value)) bound)))

    (define (symbol-hash value . rest)
      (apply string-hash (symbol->string value) rest))

    (define (default-hash value . rest)
      (cond ((boolean? value) (apply boolean-hash value rest))
            ((char? value) (apply char-hash value rest))
            ((string? value) (apply string-hash value rest))
            ((symbol? value) (apply symbol-hash value rest))
            ((number? value) (apply number-hash value rest))
            ((null? value) 0)
            (else (modulo (abs (cond ((pair? value) 31)
                                    ((vector? value) 37)
                                    ((bytevector? value) 41)
                                    (else 43)))
                          (if (pair? rest) (car rest) hash-limit)))))

    (define (comparator-register-default! comparator)
      (if (comparator? comparator) #t (error "expected a comparator" comparator)))

    (define (make-default-comparator)
      (make-comparator (lambda (value) #t) equal? #f default-hash))

    (define default-comparator (make-default-comparator))

    (define (make-eq-comparator)
      (make-comparator (lambda (value) #t) eq? #f
                       (lambda (value . rest) (default-hash value
                                               (if (pair? rest) (car rest)
                                                   hash-limit)))))
    (define (make-eqv-comparator)
      (make-comparator (lambda (value) #t) eqv? #f
                       (lambda (value . rest) (default-hash value
                                               (if (pair? rest) (car rest)
                                                   hash-limit)))))
    (define (make-equal-comparator)
      (make-comparator (lambda (value) #t) equal? #f
                       (lambda (value . rest) (default-hash value
                                               (if (pair? rest) (car rest)
                                                   hash-limit)))))

    (define eq-comparator (make-eq-comparator))
    (define eqv-comparator (make-eqv-comparator))
    (define equal-comparator (make-equal-comparator))

    (define (make-pair-comparator car-comparator cdr-comparator)
      (make-comparator
       pair?
       (lambda (left right)
         (and ((comparator-equality-predicate car-comparator)
               (car left) (car right))
              ((comparator-equality-predicate cdr-comparator)
               (cdr left) (cdr right))))
       #f
       (lambda (value . rest)
         (let ((bound (if (pair? rest) (car rest) hash-limit)))
           (modulo (+ (* 31 (comparator-hash car-comparator (car value) bound))
                      (comparator-hash cdr-comparator (cdr value) bound))
                   bound)))))

    (define (make-list-comparator element-comparator type-test empty? head tail)
      (letrec
          ((equal-lists?
            (lambda (left right)
              (cond ((empty? left) (empty? right))
                    ((empty? right) #f)
                    (else
                     (and ((comparator-equality-predicate element-comparator)
                           (head left) (head right))
                          (equal-lists? (tail left) (tail right))))))))
        (make-comparator
         type-test equal-lists? #f
         (lambda (value . rest)
           (let ((bound (if (pair? rest) (car rest) hash-limit)))
             (let loop ((items value) (h 0))
               (if (empty? items)
                   (modulo h bound)
                   (loop (tail items)
                         (modulo (+ (* 31 h)
                                    (comparator-hash element-comparator
                                                     (head items) bound))
                                 bound)))))))))

    (define (make-vector-comparator element-comparator type-test length ref)
      (make-comparator
       type-test
       (lambda (left right)
         (let ((size (length left)))
           (and (= size (length right))
                (let loop ((i 0))
                  (or (= i size)
                      (and ((comparator-equality-predicate element-comparator)
                            (ref left i) (ref right i))
                           (loop (+ i 1))))))))
       #f
       (lambda (value . rest)
         (let ((bound (if (pair? rest) (car rest) hash-limit)))
           (let loop ((i 0) (h 0))
             (if (= i (length value))
                 (modulo h bound)
                 (loop (+ i 1)
                       (modulo (+ (* 31 h)
                                  (comparator-hash element-comparator
                                                   (ref value i) bound))
                               bound))))))))

    (define list-comparator
      (make-list-comparator default-comparator list? null? car cdr))
    (define vector-comparator
      (make-vector-comparator default-comparator vector? vector-length
                              vector-ref))
    (define boolean-comparator
      (make-comparator boolean? eq? #f
                       (lambda (value . rest) (apply boolean-hash value rest))))
    (define real-comparator
      (make-comparator real? = <
                       (lambda (value . rest) (apply number-hash value rest))))
    (define char-comparator
      (make-comparator char? char=? char<?
                       (lambda (value . rest) (apply char-hash value rest))))
    (define char-ci-comparator
      (make-comparator char? char-ci=? char-ci<?
                       (lambda (value . rest) (apply char-ci-hash value rest))))
    (define string-comparator
      (make-comparator string? string=? string<?
                       (lambda (value . rest) (apply string-hash value rest))))
    (define string-ci-comparator
      (make-comparator string? string-ci=? string-ci<?
                       (lambda (value . rest) (apply string-ci-hash value rest))))

    (define (check-comparator comparator)
      (if (comparator? comparator)
          comparator
          (error "expected a comparator" comparator)))

    (define (comparator-hash comparator object)
      (let ((hash (comparator-hash-function comparator)))
        (if hash
            (begin
              (comparator-check-type comparator object)
              (hash object (hash-bound)))
            (error "comparator is not hashable" comparator))))

    (define (=? comparator left right)
      (check-comparator comparator)
      (comparator-check-type comparator left)
      (comparator-check-type comparator right)
      ((comparator-equality-predicate comparator) left right))

    (define (<? comparator left right)
      (check-comparator comparator)
      (let ((ordering (comparator-ordering-predicate comparator)))
        (if ordering
            (begin
              (comparator-check-type comparator left)
              (comparator-check-type comparator right)
              (ordering left right))
            (error "comparator is not ordered" comparator))))

    (define (>? comparator left right) (<? comparator right left))
    (define (<=? comparator left right)
      (or (=? comparator left right) (<? comparator left right)))
    (define (>=? comparator left right)
      (or (=? comparator left right) (<? comparator right left)))

    (define (comparator-max comparator . values)
      (let loop ((items values) (best (car values)))
        (cond ((null? items) best)
              ((<? comparator best (car items)) (loop (cdr items) (car items)))
              (else (loop (cdr items) best)))))
    (define (comparator-min comparator . values)
      (let loop ((items values) (best (car values)))
        (cond ((null? items) best)
              ((<? comparator (car items) best) (loop (cdr items) (car items)))
              (else (loop (cdr items) best)))))
    (define (comparator-max-in-list comparator values)
      (apply comparator-max comparator values))
    (define (comparator-min-in-list comparator values)
      (apply comparator-min comparator values))

    (define-syntax comparator-if<=>
      (syntax-rules ()
        ((_ comparator left right less equal greater)
         (cond ((<? comparator left right) less)
               ((=? comparator left right) equal)
               (else greater)))
        ((_ comparator left right less equal)
         (comparator-if<=> comparator left right less equal less))
        ((_ comparator left right less)
         (comparator-if<=> comparator left right less less less)))))
)
