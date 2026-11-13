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
          (only (scheme inexact) finite?)
          (only (npl hashtable)
                string-hash string-ci-hash hash hash-by-identity hash-by-eqv))
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

    (define (rational-hash value bound)
      (let ((exact-value (exact value)))
        (modulo (abs (+ (* 1000003 (numerator exact-value))
                        (denominator exact-value)))
                bound)))

    ; Numerically equal exact and inexact values hash the same, and the
    ; non-finite values have stable constants instead of raising.
    (define (number-hash value . rest)
      (let ((bound (if (pair? rest) (car rest) hash-limit)))
        (cond ((integer? value) (rational-hash value bound))
              ((real? value)
               (if (finite? value) (rational-hash value bound) 0))
              (else
               (modulo (+ (* 31 (apply number-hash
                                       (real-part value)
                                       (if (pair? rest) rest '())))
                          (apply number-hash
                                 (imag-part value)
                                 (if (pair? rest) rest '())))
                       bound)))))

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

    (define (hash-combine bound . values)
      (let loop ((items values) (h 0))
        (if (null? items)
            (modulo h bound)
            (loop (cdr items)
                  (modulo (+ (* 31 h) (car items)) bound)))))

    (define registered-default-comparators '())

    (define (registered-comparator-for left right)
      (let loop ((items registered-default-comparators))
        (cond ((null? items) #f)
              ((and ((comparator-type-test-predicate (car items)) left)
                    ((comparator-type-test-predicate (car items)) right))
               (car items))
              (else (loop (cdr items))))))

    (define (comparator-register-default! comparator)
      (if (comparator? comparator)
          (begin
            (set! registered-default-comparators
                  (cons comparator registered-default-comparators))
            comparator)
          (error "comparator-register-default!: expected a comparator"
                 comparator)))

    (define (registered-rank value)
      (let loop ((items registered-default-comparators) (rank 9))
        (cond ((null? items) #f)
              (((comparator-type-test-predicate (car items)) value) rank)
              (else (loop (cdr items) (+ rank 1))))))

    (define (default-type-rank value)
      (let ((rank (registered-rank value)))
        (if rank
            rank
            (cond ((null? value) 0)
            ((boolean? value) 1)
            ((char? value) 2)
            ((number? value) 3)
            ((string? value) 4)
            ((symbol? value) 5)
            ((pair? value) 6)
            ((vector? value) 7)
                  ((bytevector? value) 8)
                  (else 1000)))))

    (define (default-equal? left right)
      (let ((registered (registered-comparator-for left right)))
        (if registered
            ((comparator-equality-predicate registered) left right)
            (if (= (default-type-rank left) (default-type-rank right))
                (cond ((null? left) #t)
                      ((boolean? left) (eq? left right))
                      ((char? left) (char=? left right))
                      ((number? left) (= left right))
                      ((string? left) (string=? left right))
                      ((symbol? left) (eq? left right))
                      ((pair? left)
                       (and (default-equal? (car left) (car right))
                            (default-equal? (cdr left) (cdr right))))
                      ((vector? left)
                       (and (= (vector-length left) (vector-length right))
                            (let loop ((index 0))
                              (or (= index (vector-length left))
                                  (and (default-equal? (vector-ref left index)
                                                       (vector-ref right index))
                                       (loop (+ index 1)))))))
                      ((bytevector? left)
                       (and (= (bytevector-length left)
                               (bytevector-length right))
                            (let loop ((index 0))
                              (or (= index (bytevector-length left))
                                  (and (= (bytevector-u8-ref left index)
                                          (bytevector-u8-ref right index))
                                       (loop (+ index 1)))))))
                      (else (eq? left right)))
                #f))))

    (define (default-number-less? left right)
      (cond ((and (real? left) (real? right)) (< left right))
            ((real? left)
             (let ((right-real (real-part right)))
               (cond ((< left right-real) #t)
                     ((= left right-real) (< 0 (imag-part right)))
                     (else #f))))
            ((real? right)
             (let ((left-real (real-part left)))
               (cond ((< left-real right) #t)
                     ((= left-real right) (< (imag-part left) 0))
                     (else #f))))
            (else
             (let ((left-real (real-part left))
                   (right-real (real-part right)))
               (cond ((< left-real right-real) #t)
                     ((= left-real right-real)
                      (< (imag-part left) (imag-part right)))
                     (else #f))))))

    (define (default-less? left right)
      (let ((registered (registered-comparator-for left right)))
        (if registered
            (let ((ordering (comparator-ordering-predicate registered)))
              (if ordering (ordering left right) #f))
            (let ((left-rank (default-type-rank left))
                  (right-rank (default-type-rank right)))
              (cond ((< left-rank right-rank) #t)
                    ((> left-rank right-rank) #f)
                    ((null? left) #f)
                    ((boolean? left) (and (not left) right))
                    ((char? left) (char<? left right))
                    ((number? left) (default-number-less? left right))
                    ((string? left) (string<? left right))
                    ((symbol? left)
                     (string<? (symbol->string left) (symbol->string right)))
                    ((pair? left)
                     (cond ((default-equal? (car left) (car right))
                            (default-less? (cdr left) (cdr right)))
                           (else (default-less? (car left) (car right)))))
                    ((vector? left)
                     (cond ((< (vector-length left) (vector-length right)) #t)
                           ((> (vector-length left) (vector-length right)) #f)
                           (else
                            (let loop ((index 0))
                              (cond ((= index (vector-length left)) #f)
                                    ((default-equal? (vector-ref left index)
                                                     (vector-ref right index))
                                     (loop (+ index 1)))
                                    (else
                                     (default-less? (vector-ref left index)
                                                    (vector-ref right index))))))))
                    ((bytevector? left)
                     (cond ((< (bytevector-length left)
                               (bytevector-length right)) #t)
                           ((> (bytevector-length left)
                               (bytevector-length right)) #f)
                           (else
                            (let loop ((index 0))
                              (cond ((= index (bytevector-length left)) #f)
                                    ((= (bytevector-u8-ref left index)
                                        (bytevector-u8-ref right index))
                                     (loop (+ index 1)))
                                    (else
                                     (< (bytevector-u8-ref left index)
                                        (bytevector-u8-ref right index))))))))
                    (else #f))))))

    (define (default-hash value . rest)
      (let ((bound (if (pair? rest) (car rest) hash-limit)))
        (let ((registered (registered-comparator-for value value)))
          (if (and registered (comparator-hash-function registered))
              ((comparator-hash-function registered) value)
              (cond ((boolean? value) (boolean-hash value bound))
                    ((char? value) (char-hash value bound))
                    ((string? value) (string-hash value bound))
                    ((symbol? value) (symbol-hash value bound))
                    ((number? value) (number-hash value bound))
                    ((null? value) 0)
                    ((pair? value)
                     (hash-combine bound
                                   (default-hash (car value) bound)
                                   (default-hash (cdr value) bound)))
                    ((vector? value)
                     (let loop ((index 0) (h 0))
                       (if (= index (vector-length value))
                           (modulo h bound)
                           (loop (+ index 1)
                                 (modulo (+ (* 31 h)
                                            (default-hash
                                             (vector-ref value index) bound))
                                         bound)))))
                    ((bytevector? value)
                     (let loop ((index 0) (h 0))
                       (if (= index (bytevector-length value))
                           (modulo h bound)
                           (loop (+ index 1)
                                 (modulo (+ (* 31 h)
                                            (bytevector-u8-ref value index))
                                         bound)))))
                    (else (modulo (hash-by-identity value) bound)))))))

    (define (make-default-comparator)
      (make-comparator (lambda (value) #t) default-equal? default-less?
                       default-hash))

    (define default-comparator (make-default-comparator))

    (define (make-eq-comparator)
      (make-comparator (lambda (value) #t) eq? #f
                       (lambda (value) (hash-by-identity value))))
    (define (make-eqv-comparator)
      (make-comparator (lambda (value) #t) eqv? #f
                       (lambda (value) (hash-by-eqv value))))
    (define (make-equal-comparator)
      (make-comparator (lambda (value) #t) equal? #f
                       (lambda (value) (hash value))))

    (define eq-comparator (make-eq-comparator))
    (define eqv-comparator (make-eqv-comparator))
    (define equal-comparator (make-equal-comparator))

    (define (make-pair-comparator car-comparator cdr-comparator)
      (let ((car-equal? (comparator-equality-predicate car-comparator))
            (cdr-equal? (comparator-equality-predicate cdr-comparator))
            (car-less? (comparator-ordering-predicate car-comparator))
            (cdr-less? (comparator-ordering-predicate cdr-comparator))
            (car-hash (comparator-hash-function car-comparator))
            (cdr-hash (comparator-hash-function cdr-comparator)))
        (make-comparator
         (lambda (value)
           (and (pair? value)
                ((comparator-type-test-predicate car-comparator) (car value))
                ((comparator-type-test-predicate cdr-comparator) (cdr value))))
         (lambda (left right)
           (and (car-equal? (car left) (car right))
                (cdr-equal? (cdr left) (cdr right))))
         (if (and car-less? cdr-less?)
             (lambda (left right)
               (cond ((car-equal? (car left) (car right))
                      (if (cdr-equal? (cdr left) (cdr right))
                          #f
                          (cdr-less? (cdr left) (cdr right))))
                     (else (car-less? (car left) (car right)))))
             #f)
         (if (and car-hash cdr-hash)
             (lambda (value)
               (hash-combine (hash-bound)
                             (car-hash (car value))
                             (cdr-hash (cdr value))))
             #f))))

    (define (all-elements? items empty? head tail element-test)
      (cond ((empty? items) #t)
            ((element-test (head items)) (all-elements? (tail items) empty?
                                                        head tail element-test))
            (else #f)))

    (define (make-list-comparator element-comparator type-test empty? head tail)
      (let ((element-equal? (comparator-equality-predicate element-comparator))
            (element-less? (comparator-ordering-predicate element-comparator))
            (element-hash (comparator-hash-function element-comparator)))
        (letrec
            ((equal-lists?
              (lambda (left right)
                (cond ((empty? left) (empty? right))
                      ((empty? right) #f)
                      (else
                       (and (element-equal? (head left) (head right))
                            (equal-lists? (tail left) (tail right)))))))
             (less-lists?
              (lambda (left right)
                (cond ((empty? left) (not (empty? right)))
                      ((empty? right) #f)
                      ((element-equal? (head left) (head right))
                       (less-lists? (tail left) (tail right)))
                      (else (element-less? (head left) (head right)))))))
          (make-comparator
           (lambda (value)
             (and (type-test value)
                  (all-elements? value empty? head tail
                                 (comparator-type-test-predicate
                                  element-comparator))))
           equal-lists?
           (if element-less? less-lists? #f)
           (if element-hash
               (lambda (value)
                 (let loop ((items value) (h 0))
                   (if (empty? items)
                       (modulo h (hash-bound))
                       (loop (tail items)
                             (modulo (+ (* 31 h) (element-hash (head items)))
                                     (hash-bound))))))
               #f)))))

    (define (make-vector-comparator element-comparator type-test length ref)
      (let ((element-equal? (comparator-equality-predicate element-comparator))
            (element-less? (comparator-ordering-predicate element-comparator))
            (element-hash (comparator-hash-function element-comparator)))
        (make-comparator
         (lambda (value)
           (and (type-test value)
                (let loop ((index 0))
                  (or (= index (length value))
                      (and ((comparator-type-test-predicate element-comparator)
                            (ref value index))
                           (loop (+ index 1)))))))
         (lambda (left right)
           (and (= (length left) (length right))
                (let loop ((index 0))
                  (or (= index (length left))
                      (and (element-equal? (ref left index) (ref right index))
                           (loop (+ index 1)))))))
         (if element-less?
             (lambda (left right)
               (cond ((< (length left) (length right)) #t)
                     ((> (length left) (length right)) #f)
                     (else
                      (let loop ((index 0))
                        (cond ((= index (length left)) #f)
                              ((element-equal? (ref left index)
                                               (ref right index))
                               (loop (+ index 1)))
                              (else
                               (element-less? (ref left index)
                                              (ref right index))))))))
             #f)
         (if element-hash
             (lambda (value)
               (let loop ((index 0) (h 0))
                 (if (= index (length value))
                     (modulo h (hash-bound))
                     (loop (+ index 1)
                           (modulo (+ (* 31 h) (element-hash (ref value index)))
                                   (hash-bound))))))
             #f))))

    (define list-comparator
      (make-list-comparator default-comparator list? null? car cdr))
    (define vector-comparator
      (make-vector-comparator default-comparator vector? vector-length
                              vector-ref))
    (define boolean-comparator
      (make-comparator boolean? eq? (lambda (left right) (and (not left) right))
                       boolean-hash))
    (define real-comparator
      (make-comparator real? = < number-hash))
    (define char-comparator
      (make-comparator char? char=? char<? char-hash))
    (define char-ci-comparator
      (make-comparator char? char-ci=? char-ci<? char-ci-hash))
    (define string-comparator
      (make-comparator string? string=? string<? string-hash))
    (define string-ci-comparator
      (make-comparator string? string-ci=? string-ci<?
                       string-ci-hash))

    (define (check-comparator comparator)
      (if (comparator? comparator)
          comparator
          (error "expected a comparator" comparator)))

    (define (comparator-hash comparator object)
      (let ((hash (comparator-hash-function comparator)))
        (if hash
            (begin
              (comparator-check-type comparator object)
              (hash object))
            (error "comparator is not hashable" comparator))))

    (define (comparison-pair comparator who left right)
      (check-comparator comparator)
      (comparator-check-type comparator left)
      (comparator-check-type comparator right)
      (if (null? (comparator-ordering-predicate comparator))
          (error "comparator is not ordered" who)))

    (define (=? comparator left right . rest)
      (check-comparator comparator)
      (comparator-check-type comparator left)
      (comparator-check-type comparator right)
      (if ((comparator-equality-predicate comparator) left right)
          (or (null? rest) (apply =? comparator right rest))
          #f))

    (define (ordered-chain who ordering? comparator left right rest)
      (comparison-pair comparator who left right)
      (if (ordering? comparator left right)
          (or (null? rest)
              (ordered-chain who ordering? comparator right (car rest)
                             (cdr rest)))
          #f))

    (define (<? comparator left right . rest)
      (ordered-chain "<?"
                     (lambda (c l r)
                       ((comparator-ordering-predicate c) l r))
                     comparator left right rest))

    (define (>? comparator left right . rest)
      (ordered-chain ">?"
                     (lambda (c l r)
                       ((comparator-ordering-predicate c) r l))
                     comparator left right rest))

    (define (<=? comparator left right . rest)
      (ordered-chain "<=?"
                     (lambda (c l r)
                       (or ((comparator-equality-predicate c) l r)
                           ((comparator-ordering-predicate c) l r)))
                     comparator left right rest))

    (define (>=? comparator left right . rest)
      (ordered-chain ">=?"
                     (lambda (c l r)
                       (or ((comparator-equality-predicate c) l r)
                           ((comparator-ordering-predicate c) r l)))
                     comparator left right rest))

    (define (comparator-max comparator . values)
      (let loop ((items (cdr values)) (best (car values)))
        (cond ((null? items) best)
              ((<? comparator best (car items)) (loop (cdr items) (car items)))
              (else (loop (cdr items) best)))))
    (define (comparator-min comparator . values)
      (let loop ((items (cdr values)) (best (car values)))
        (cond ((null? items) best)
              ((<? comparator (car items) best) (loop (cdr items) (car items)))
              (else (loop (cdr items) best)))))
    (define (comparator-max-in-list comparator values)
      (apply comparator-max comparator values))
    (define (comparator-min-in-list comparator values)
      (apply comparator-min comparator values))

    (define-syntax comparator-if<=>
      (syntax-rules ()
        ((_ left right less equal greater)
         (comparator-if<=> default-comparator left right less equal greater))
        ((_ comparator left right less equal greater)
         (cond ((<? comparator left right) less)
               ((=? comparator left right) equal)
               (else greater))))))
  )
