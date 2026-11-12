; SRFI 47 arrays with the canonical NPL library name.
;
; Rank-1 arrays are ordinary vectors and strings; rank-n literal arrays such
; as #2A((1 2) (3 4)) read as nested vectors. make-shared-array and rank-0
; arrays (including the uniform-array prototypes) use an array record.
; array? accepts vectors and strings too, as SRFI 47 specifies.
(define-library (npl data array)
  (export array? make-array make-shared-array
          array-rank array-dimensions array-in-bounds?
          array-ref array-set!
          ac64 ac32 ar64 ar32 as64 as32 as16 as8
          au64 au32 au16 au8 at1)
  (import (scheme base))
  (begin
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

    (define (array? value)
      (or (vector? value) (string? value)
          (array-record? value) (prototype? value)))

    (define (array-error who message . irritants)
      (apply error (string-append who ": " message) irritants))

    (define (exact-natural? value)
      (and (exact-integer? value) (<= 0 value)))

    (define (vector-shape vector who)
      (if (zero? (vector-length vector))
          (list 0)
          (let ((first (vector-ref vector 0)))
            (if (vector? first)
                (let ((inner (vector-shape first who)))
                  (let check ((i 1))
                    (if (< i (vector-length vector))
                        (let ((item (vector-ref vector i)))
                          (if (and (vector? item)
                                   (equal? (vector-shape item who) inner))
                              (check (+ i 1))
                              (array-error who "non-rectangular array" vector))))
                    (cons (vector-length vector) inner)))
                (begin
                  (let check ((i 1))
                    (if (< i (vector-length vector))
                        (if (vector? (vector-ref vector i))
                            (array-error who "non-rectangular array" vector)
                            (check (+ i 1)))))
                  (list (vector-length vector)))))))

    (define (array-rank value)
      (cond ((array-record? value)
             (length (array-dimensions-list value)))
            ((prototype? value) 0)
            ((string? value) 1)
            ((vector? value) (length (vector-shape value "array-rank")))
            (else 0)))

    (define (array-dimensions value)
      (cond ((array-record? value) (array-dimensions-list value))
            ((prototype? value) '())
            ((string? value) (list (string-length value)))
            ((vector? value) (vector-shape value "array-dimensions"))
            (else (array-error "array-dimensions" "expected an array" value))))

    (define (indices-in-bounds? dimensions indices)
      (cond ((null? dimensions) (null? indices))
            ((null? indices) #f)
            ((not (exact-integer? (car indices))) #f)
            ((not (<= 0 (car indices) (- (car dimensions) 1))) #f)
            (else (indices-in-bounds? (cdr dimensions) (cdr indices)))))

    (define (array-in-bounds? value . indices)
      (and (array? value)
           (indices-in-bounds? (array-dimensions value) indices)))

    (define (check-indices who dimensions indices)
      (if (indices-in-bounds? dimensions indices)
          indices
          (array-error who "index out of bounds" indices)))

    (define (%nested-ref who value indices)
      (if (null? indices)
          value
          (cond ((vector? value)
                 (%nested-ref who
                              (vector-ref value (car indices))
                              (cdr indices)))
                ((string? value)
                 (if (null? (cdr indices))
                     (string-ref value (car indices))
                     (array-error who "string array has rank 1" indices)))
                (else
                 (array-error who "too many indices" indices)))))

    (define (%nested-set! who value indices item)
      (if (null? (cdr indices))
          (cond ((vector? value)
                 (vector-set! value (car indices) item))
                ((string? value)
                 (string-set! value (car indices) item))
                (else (array-error who "not an array" value)))
          (let ((next (if (vector? value)
                          (vector-ref value (car indices))
                          (array-error who "not an array" value))))
            (%nested-set! who next (cdr indices) item))))

    (define (array-ref value . indices)
      (cond ((array-record? value)
             (cond ((eq? (array-kind value) 'shared)
                    (let ((mapped (apply (array-mapper value) indices)))
                      (apply array-ref (array-base value) mapped)))
                   ((null? indices) (array-content value))
                   (else
                    (array-error "array-ref" "rank-0 array takes no indices"
                                 indices))))
            ((prototype? value)
             (if (null? indices)
                 (prototype-value value)
                 (array-error "array-ref" "prototype takes no indices")))
            ((or (vector? value) (string? value))
             (check-indices "array-ref" (array-dimensions value) indices)
             (%nested-ref "array-ref" value indices))
            (else (array-error "array-ref" "expected an array" value))))

    (define (array-set! value item . indices)
      (cond ((array-record? value)
             (cond ((eq? (array-kind value) 'shared)
                    (let ((mapped (apply (array-mapper value) indices)))
                      (apply array-set! (array-base value) item mapped)))
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
            ((or (vector? value) (string? value))
             (check-indices "array-set!" (array-dimensions value) indices)
             (%nested-set! "array-set!" value indices item)
             item)
            (else (array-error "array-set!" "expected an array" value))))

    (define (check-dimensions who dimensions)
      (let loop ((items dimensions))
        (cond ((null? items) #t)
              ((exact-natural? (car items)) (loop (cdr items)))
              (else (array-error who "invalid dimension" (car items))))))

    (define (prototype-fill prototype)
      (cond ((prototype? prototype) (prototype-value prototype))
            ((array-record? prototype) (array-content prototype))
            ((vector? prototype)
             (if (zero? (vector-length prototype))
                 0
                 (apply array-ref prototype
                        (make-list (array-rank prototype) 0))))
            ((string? prototype)
             (if (zero? (string-length prototype)) #\space
                 (string-ref prototype 0)))
            (else (array-error "make-array" "invalid prototype" prototype))))

    (define (make-nested dimensions fill)
      (if (null? (cdr dimensions))
          (make-vector (car dimensions) fill)
          (let ((result (make-vector (car dimensions))))
            (let loop ((i 0))
              (if (< i (car dimensions))
                  (begin
                    (vector-set! result i (make-nested (cdr dimensions) fill))
                    (loop (+ i 1)))))
            result)))

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
              (else
               (make-nested dimensions fill)))))

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
