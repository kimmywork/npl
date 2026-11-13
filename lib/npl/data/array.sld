; SRFI 47 arrays with the canonical NPL library name.
;
; Rank-1 arrays are ordinary vectors and strings. A rank-n array (including
; every #nA literal) is the vector [npl-array-tag shape data], where shape is
; a vector of dimensions and data is a flat row-major vector. Keeping the
; shape explicit means ordinary vectors stay rank-1 even when their elements
; are vectors, and zero-length dimensions keep their full shape.
; make-shared-array, rank-0 arrays, and uniform prototypes use records.
(define-library (npl data array)
  (export array? make-array make-shared-array
          array-rank array-dimensions array-in-bounds?
          array-ref array-set!
          ac64 ac32 ar64 ar32 as64 as32 as16 as8
          au64 au32 au16 au8 at1)
  (import (scheme base))
  (begin
    (define ARRAY-TAG 'npl-array-tag)

    (define-record-type array-record
      (make-array-record kind base mapper dimensions content)
      array-record?
      (kind array-kind)
      (base array-base)
      (mapper array-mapper)
      (dimensions array-dimensions-list)
      (content array-content set-array-content!))

    (define-record-type prototype-record
      (make-prototype kind value)
      prototype?
      (kind prototype-kind)
      (value prototype-value set-prototype-value!))

    (define (array-literal? value)
      (and (vector? value)
           (= (vector-length value) 3)
           (eq? (vector-ref value 0) ARRAY-TAG)))

    (define (array? value)
      (or (vector? value) (string? value)
          (array-record? value) (prototype? value)))

    (define (array-error who message . irritants)
      (apply error (string-append who ": " message) irritants))

    (define (exact-natural? value)
      (and (exact-integer? value) (<= 0 value)))

    (define (array-rank value)
      (cond ((array-literal? value)
             (vector-length (vector-ref value 1)))
            ((array-record? value)
             (length (array-dimensions-list value)))
            ((prototype? value) 0)
            ((vector? value) 1)
            ((string? value) 1)
            (else 0)))

    (define (array-dimensions value)
      (cond ((array-literal? value)
             (vector->list (vector-ref value 1)))
            ((array-record? value)
             (array-dimensions-list value))
            ((prototype? value) '())
            ((vector? value) (list (vector-length value)))
            ((string? value) (list (string-length value)))
            (else
             (array-error "array-dimensions" "expected an array" value))))

    (define (indices-in-bounds? dimensions indices)
      (cond ((null? dimensions) (null? indices))
            ((null? indices) #f)
            ((not (exact-integer? (car indices))) #f)
            ((not (<= 0 (car indices) (- (car dimensions) 1))) #f)
            (else (indices-in-bounds? (cdr dimensions) (cdr indices)))))

    (define (check-indices who dimensions indices)
      (if (indices-in-bounds? dimensions indices)
          indices
          (array-error who "index out of bounds" indices)))

    (define (array-in-bounds? value . indices)
      (and (array? value)
           (indices-in-bounds? (array-dimensions value) indices)))

    (define (flat-index shape indices)
      (let loop ((i 0) (index 0))
        (if (= i (vector-length shape))
            index
            (loop (+ i 1)
                  (+ (* index (vector-ref shape i))
                     (list-ref indices i))))))

    (define (array-ref value . indices)
      (cond ((array-literal? value)
             (check-indices "array-ref" (array-dimensions value) indices)
             (vector-ref (vector-ref value 2)
                         (flat-index (vector-ref value 1) indices)))
            ((array-record? value)
             (cond ((eq? (array-kind value) 'shared)
                    (check-indices "array-ref"
                                   (array-dimensions-list value)
                                   indices)
                    (apply array-ref (array-base value)
                           (apply (array-mapper value) indices)))
                   ((null? indices) (array-content value))
                   (else
                    (array-error "array-ref" "rank-0 array takes no indices"
                                 indices))))
            ((prototype? value)
             (if (null? indices)
                 (prototype-value value)
                 (array-error "array-ref" "prototype takes no indices")))
            ((vector? value)
             (if (null? (cdr indices))
                 (begin
                   (check-indices "array-ref" (list (vector-length value))
                                  indices)
                   (vector-ref value (car indices)))
                 (array-error "array-ref" "vector array has rank 1" indices)))
            ((string? value)
             (if (null? (cdr indices))
                 (begin
                   (check-indices "array-ref" (list (string-length value))
                                  indices)
                   (string-ref value (car indices)))
                 (array-error "array-ref" "string array has rank 1" indices)))
            (else (array-error "array-ref" "expected an array" value))))

    (define (array-set! value item . indices)
      (cond ((array-literal? value)
             (check-indices "array-set!" (array-dimensions value) indices)
             (vector-set! (vector-ref value 2)
                          (flat-index (vector-ref value 1) indices)
                          item)
             item)
            ((array-record? value)
             (cond ((eq? (array-kind value) 'shared)
                    (check-indices "array-set!"
                                   (array-dimensions-list value)
                                   indices)
                    (apply array-set! (array-base value) item
                           (apply (array-mapper value) indices)))
                   ((null? indices)
                    (set-array-content! value item)
                    item)
                   (else
                    (array-error "array-set!"
                                 "rank-0 array takes no indices" indices))))
            ((prototype? value)
             (if (null? indices)
                 (begin (set-prototype-value! value item) item)
                 (array-error "array-set!" "prototype takes no indices")))
            ((vector? value)
             (if (null? (cdr indices))
                 (begin
                   (check-indices "array-set!" (list (vector-length value))
                                  indices)
                   (vector-set! value (car indices) item)
                   item)
                 (array-error "array-set!" "vector array has rank 1" indices)))
            ((string? value)
             (if (null? (cdr indices))
                 (begin
                   (check-indices "array-set!" (list (string-length value))
                                  indices)
                   (string-set! value (car indices) item)
                   item)
                 (array-error "array-set!" "string array has rank 1" indices)))
            (else (array-error "array-set!" "expected an array" value))))

    (define (check-dimensions who dimensions)
      (let loop ((items dimensions))
        (cond ((null? items) #t)
              ((exact-natural? (car items)) (loop (cdr items)))
              (else (array-error who "invalid dimension" (car items))))))

    (define (prototype-fill prototype)
      (cond ((prototype? prototype) (prototype-value prototype))
            ((array-record? prototype)
             (apply array-ref prototype
                    (make-list (array-rank prototype) 0)))
            ((array-literal? prototype)
             (apply array-ref prototype
                    (make-list (array-rank prototype) 0)))
            ((vector? prototype)
             (if (zero? (vector-length prototype))
                 0
                 (vector-ref prototype 0)))
            ((string? prototype)
             (if (zero? (string-length prototype))
                 #\space
                 (string-ref prototype 0)))
            (else (array-error "make-array" "invalid prototype" prototype))))

    (define (shape-product dimensions)
      (let loop ((items dimensions) (total 1))
        (if (null? items)
            total
            (loop (cdr items) (* total (car items))))))

    (define (make-array prototype . dimensions)
      (check-dimensions "make-array" dimensions)
      (let ((fill (prototype-fill prototype)))
        (cond ((null? dimensions)
               (make-array-record 'rank0 #f #f '() fill))
              ((and (string? prototype) (= 1 (length dimensions)))
               (if (char? fill)
                   (make-string (car dimensions) fill)
                   (array-error "make-array"
                                "string prototype needs a character" fill)))
              ((= 1 (length dimensions))
               (make-vector (car dimensions) fill))
              (else
               (vector ARRAY-TAG
                       (list->vector dimensions)
                       (make-vector (shape-product dimensions) fill))))))

    (define (make-shared-array value mapper . dimensions)
      (if (not (array? value))
          (array-error "make-shared-array" "expected an array" value))
      (check-dimensions "make-shared-array" dimensions)
      (make-array-record 'shared value mapper dimensions #f))

    (define (uniform-prototype kind default)
      (lambda args
        (make-prototype kind (if (pair? args) (car args) default))))

    (define ac64 (uniform-prototype 'ac64 0.0))
    (define ac32 (uniform-prototype 'ac32 0.0))
    (define ar64 (uniform-prototype 'ar64 0.0))
    (define ar32 (uniform-prototype 'ar32 0.0))
    (define as64 (uniform-prototype 'as64 0))
    (define as32 (uniform-prototype 'as32 0))
    (define as16 (uniform-prototype 'as16 0))
    (define as8 (uniform-prototype 'as8 0))
    (define au64 (uniform-prototype 'au64 0))
    (define au32 (uniform-prototype 'au32 0))
    (define au16 (uniform-prototype 'au16 0))
    (define au8 (uniform-prototype 'au8 0))
    (define at1 (uniform-prototype 'at1 #f))))
