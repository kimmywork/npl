; (scheme base) vector and bytevector procedures (R7RS 6.8, 6.9).
; Built on the vector/bytevector primitives; optional start/end arguments
; follow R7RS ranges and are validated before any mutation.

(define (range-error who) (raise (string-append who ": invalid range")))

(define (index-argument values default)
  (if (pair? values) (car values) default))

; Returns (start . end) for optional (start end) within [0, length].
(define (checked-range who length optional)
  (let ((start (index-argument optional 0))
        (end (if (if (pair? optional) (pair? (cdr optional)) #f)
                 (car (cdr optional))
                 length)))
    (if (if (exact-integer? start) (exact-integer? end) #f)
        (if (if (raw-less-equal 0 start) (raw-less-equal start end) #f)
            (if (raw-less-equal end length) (cons start end) (range-error who))
            (range-error who))
        (range-error who))))

; ---------------- vectors ----------------

(define (make-vector count . fill)
  (if (if (exact-integer? count) (raw-less-equal 0 count) #f)
      (list->vector (make-list count (index-argument fill #f)))
      (raise "make-vector: expected a non-negative exact count")))

(define (vector-slice->list source start end output)
  (if (raw-equal start end)
      output
      (vector-slice->list source start (raw-subtract end 1)
                          (cons (vector-ref source (raw-subtract end 1))
                                output))))

(define (vector->list source . range)
  (let ((bounds (checked-range "vector->list" (vector-length source) range)))
    (vector-slice->list source (car bounds) (cdr bounds) '())))

(define (vector-copy source . range)
  (list->vector (apply vector->list source range)))

(define (vector-fill-range! target fill index end)
  (if (raw-less index end)
      (begin
        (vector-set! target index fill)
        (vector-fill-range! target fill (raw-add index 1) end))))

(define (vector-fill! target fill . range)
  (let ((bounds (checked-range "vector-fill!" (vector-length target) range)))
    (vector-fill-range! target fill (car bounds) (cdr bounds))))

; Copies through a list, so overlapping source and destination are safe.
(define (vector-store! target at items)
  (if (pair? items)
      (begin
        (vector-set! target at (car items))
        (vector-store! target (raw-add at 1) (cdr items)))))

(define (vector-copy! target at source . range)
  (let ((bounds (checked-range "vector-copy!" (vector-length source) range)))
    (if (if (exact-integer? at)
            (if (raw-less-equal 0 at)
                (raw-less-equal (raw-add at (raw-subtract (cdr bounds)
                                                          (car bounds)))
                                (vector-length target))
                #f)
            #f)
        (vector-store! target at
          (vector-slice->list source (car bounds) (cdr bounds) '()))
        (range-error "vector-copy!"))))

(define (vector-append . vectors)
  (list->vector (apply append (map vector->list vectors))))

(define (vector-map procedure first . rest)
  (list->vector
    (apply map procedure (vector->list first) (map vector->list rest))))

(define (vector-for-each procedure first . rest)
  (apply for-each procedure (vector->list first) (map vector->list rest)))

; ---------------- bytevectors ----------------

(define (make-bytevector count . fill)
  (if (if (exact-integer? count) (raw-less-equal 0 count) #f)
      (apply bytevector (make-list count (index-argument fill 0)))
      (raise "make-bytevector: expected a non-negative exact count")))

(define (bytevector-slice->list source start end output)
  (if (raw-equal start end)
      output
      (bytevector-slice->list source start (raw-subtract end 1)
        (cons (bytevector-u8-ref source (raw-subtract end 1)) output))))

(define (bytevector-range->list who source range)
  (let ((bounds (checked-range who (bytevector-length source) range)))
    (bytevector-slice->list source (car bounds) (cdr bounds) '())))

(define (bytevector-copy source . range)
  (apply bytevector (bytevector-range->list "bytevector-copy" source range)))

(define (bytevector-store! target at bytes)
  (if (pair? bytes)
      (begin
        (bytevector-u8-set! target at (car bytes))
        (bytevector-store! target (raw-add at 1) (cdr bytes)))))

(define (bytevector-copy! target at source . range)
  (let ((bytes (bytevector-range->list "bytevector-copy!" source range)))
    (if (if (exact-integer? at)
            (if (raw-less-equal 0 at)
                (raw-less-equal (raw-add at (length bytes))
                                (bytevector-length target))
                #f)
            #f)
        (bytevector-store! target at bytes)
        (range-error "bytevector-copy!"))))

(define (bytevector-append . bytevectors)
  (apply bytevector
    (apply append
      (map (lambda (b) (bytevector-range->list "bytevector-append" b '()))
           bytevectors))))
