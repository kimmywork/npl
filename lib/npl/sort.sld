; SRFI 132 sort libraries and the SRFI 95 generic sorting names, with the
; canonical NPL library name.
;
; The SRFI 132 procedures are type-specific and take a two-argument less?
; predicate. The SRFI 95 names (sort, sort!, sorted?, merge, merge!) are
; generic over lists, vectors, and strings and accept an optional key.
(define-library (npl sort)
  (export
    list-sorted? vector-sorted?
    list-sort list-stable-sort list-sort! list-stable-sort!
    vector-sort vector-stable-sort vector-sort! vector-stable-sort!
    list-merge list-merge! vector-merge vector-merge!
    list-delete-neighbor-dups list-delete-neighbor-dups!
    vector-delete-neighbor-dups vector-delete-neighbor-dups!
    vector-find-median vector-find-median!
    vector-select! vector-separate!
    sorted? merge merge! sort sort!)
  (import (scheme base) (scheme char) (npl list))
  (begin
    (define (keyed key value)
      (if key (cons (key value) value) (cons value value)))

    (define (decorate-list key items)
      (if (null? items)
          '()
          (cons (keyed key (car items)) (decorate-list key (cdr items)))))

    (define (decorate-vector vec key start end)
      (let loop ((i (- end 1)) (result '()))
        (if (< i start)
            result
            (loop (- i 1) (cons (keyed key (vector-ref vec i)) result)))))

    (define (merge-two less? left right)
      (cond ((null? left) right)
            ((null? right) left)
            ((less? (car (car right)) (car (car left)))
             (cons (car right) (merge-two less? left (cdr right))))
            (else
             (cons (car left) (merge-two less? (cdr left) right)))))

    (define (merge-sort less? items)
      (let ((length (length items)))
        (if (< length 2)
            items
            (let* ((half (quotient length 2))
                   (left (take items half))
                   (right (drop items half)))
              (merge-two less? (merge-sort less? left)
                         (merge-sort less? right))))))

    (define (values-of items)
      (if (null? items) '() (cons (cdr (car items)) (values-of (cdr items)))))

    ; SRFI 95 allows at most one key call per element.
    (define (key-cache key)
      (let ((cache '()))
        (lambda (element)
          (let ((hit (assq element cache)))
            (if hit
                (cdr hit)
                (let ((value (if key (key element) element)))
                  (set! cache (cons (cons element value) cache))
                  value))))))

    (define (list-sort-range less? items key)
      (values-of (merge-sort less? (decorate-list key items))))

    (define (vector-range who vector start end)
      (if (if (exact-integer? start) (exact-integer? end) #f)
          (if (if (<= 0 start end) (<= end (vector-length vector)) #f)
              (cons start end)
              (error who "invalid range" start end))
          (error who "invalid range" start end)))

    (define (list-sorted? less? items)
      (cond ((null? items) #t)
            ((null? (cdr items)) #t)
            ((less? (car (cdr items)) (car items)) #f)
            (else (list-sorted? less? (cdr items)))))

    (define (vector-sorted? less? vector . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (car (cdr rest)) (vector-length vector)))
             (range (vector-range "vector-sorted?" vector start end)))
        (let loop ((i start))
          (if (>= (+ i 1) end)
              #t
              (if (less? (vector-ref vector (+ i 1))
                         (vector-ref vector i))
                  #f
                  (loop (+ i 1)))))))

    (define (list-sort less? items)
      (list-sort-range less? items #f))

    (define (list-stable-sort less? items)
      (list-sort-range less? items #f))

    (define (list-sort! less? items)
      (let ((sorted (list-sort less? items)))
        (let loop ((target items) (source sorted))
          (if (pair? target)
              (begin
                (set-car! target (car source))
                (loop (cdr target) (cdr source)))))
        items))

    (define (list-stable-sort! less? items)
      (list-sort! less? items))

    (define (vector-sort less? vector . rest)
      (list->vector
       (values-of
        (merge-sort less?
                    (decorate-vector vector #f
                      (if (pair? rest) (car rest) 0)
                      (if (and (pair? rest) (pair? (cdr rest)))
                          (car (cdr rest))
                          (vector-length vector)))))))

    (define (vector-stable-sort less? vector . rest)
      (apply vector-sort less? vector rest))

    (define (write-sorted-values! target start values)
      (let loop ((i start) (items values))
        (if (pair? items)
            (begin
              (vector-set! target i (car items))
              (loop (+ i 1) (cdr items)))))
      target)

    (define (vector-sort! less? vector . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (car (cdr rest)) (vector-length vector)))
             (sorted (merge-sort
                      less? (decorate-vector vector #f start end))))
        (write-sorted-values! vector start (values-of sorted))))

    (define (vector-stable-sort! less? vector . rest)
      (apply vector-sort! less? vector rest))

    (define (list-merge less? left right)
      (values-of (merge-two less?
                            (decorate-list #f left)
                            (decorate-list #f right))))

    (define (list-merge! less? left right)
      (cond ((null? left) right)
            ((null? right) left)
            ((less? (car right) (car left))
             (let ((next (cdr right)))
               (set-cdr! right (list-merge! less? left next))
               right))
            (else
             (let ((next (cdr left)))
               (set-cdr! left (list-merge! less? next right))
               left))))

    (define (vector-merge less? left right . rest)
      (let* ((lstart (if (pair? rest) (car rest) 0))
             (lend (if (and (pair? rest) (pair? (cdr rest)))
                       (car (cdr rest)) (vector-length left)))
             (rstart (if (and (pair? rest) (pair? (cdr rest))
                              (pair? (cdr (cdr rest))))
                         (car (cdr (cdr rest))) 0))
             (rend (if (and (pair? rest) (pair? (cdr rest))
                            (pair? (cdr (cdr rest)))
                            (pair? (cdr (cdr (cdr rest)))))
                       (car (cdr (cdr (cdr rest))))
                       (vector-length right)))
             (llist (decorate-vector left #f lstart lend))
             (rlist (decorate-vector right #f rstart rend)))
        (list->vector (values-of (merge-two less? llist rlist)))))

    (define (vector-merge! less? target left right . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (lstart (if (and (pair? rest) (pair? (cdr rest)))
                         (car (cdr rest)) 0))
             (lend (if (and (pair? rest) (pair? (cdr rest))
                            (pair? (cdr (cdr rest))))
                       (car (cdr (cdr rest))) (vector-length left)))
             (rstart (if (and (pair? rest) (pair? (cdr rest))
                              (pair? (cdr (cdr rest)))
                              (pair? (cdr (cdr (cdr rest)))))
                         (car (cdr (cdr (cdr rest)))) 0))
             (rend (if (and (pair? rest) (pair? (cdr rest))
                            (pair? (cdr (cdr rest)))
                            (pair? (cdr (cdr (cdr rest))))
                            (pair? (cdr (cdr (cdr (cdr rest))))))
                       (car (cdr (cdr (cdr (cdr rest)))))
                       (vector-length right)))
             (merged (merge-two
                      less? (decorate-vector left #f lstart lend)
                      (decorate-vector right #f rstart rend))))
        (write-sorted-values! target start (values-of merged))))

    (define (list-delete-neighbor-dups same? items)
      (if (null? items)
          '()
          (let loop ((items (cdr items)) (previous (car items)) (result (list (car items))))
            (cond ((null? items) (reverse result))
                  ((same? previous (car items))
                   (loop (cdr items) previous result))
                  (else
                   (loop (cdr items) (car items) (cons (car items) result)))))))

    (define (list-delete-neighbor-dups! same? items)
      (if (null? items)
          '()
          (let loop ((previous items) (rest (cdr items)))
            (cond ((null? rest) items)
                  ((same? (car previous) (car rest))
                   (set-cdr! previous (cdr rest))
                   (loop previous (cdr rest)))
                  (else (loop rest (cdr rest)))))))

    (define (vector-delete-neighbor-dups same? vector . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (car (cdr rest)) (vector-length vector)))
             (range (vector-range "vector-delete-neighbor-dups" vector start end))
             (kept '()))
        (let loop ((i (car range)) (previous #f) (first #t) (kept '()))
          (if (< i (cdr range))
              (let ((value (vector-ref vector i)))
                (if (or first (not (same? previous value)))
                    (loop (+ i 1) value #f (cons value kept))
                    (loop (+ i 1) value #f kept)))
              (list->vector (reverse kept))))))

    (define (vector-delete-neighbor-dups! same? vector . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (car (cdr rest)) (vector-length vector)))
             (kept (vector-delete-neighbor-dups same? vector start end)))
        (let loop ((i 0))
          (if (< i (vector-length kept))
              (begin
                (vector-set! vector (+ start i) (vector-ref kept i))
                (loop (+ i 1)))))
        (+ start (vector-length kept))))

    (define (vector-find-median less? vector knil . rest)
      (let* ((mean (if (pair? rest) (car rest)
                       (lambda (left right) (/ (+ left right) 2))))
             (sorted (vector-sort less? vector))
             (length (vector-length sorted)))
        (cond ((zero? length) knil)
              ((odd? length) (vector-ref sorted (quotient length 2)))
              (else (mean (vector-ref sorted (- (quotient length 2) 1))
                          (vector-ref sorted (quotient length 2)))))))

    (define (vector-find-median! less? vector knil . rest)
      (let ((mean (if (pair? rest) (car rest)
                      (lambda (left right) (/ (+ left right) 2)))))
        (vector-sort! less? vector)
        (let ((length (vector-length vector)))
          (cond ((zero? length) knil)
                ((odd? length) (vector-ref vector (quotient length 2)))
                (else (mean (vector-ref vector (- (quotient length 2) 1))
                            (vector-ref vector (quotient length 2))))))))

    (define (vector-select! less? vector k . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (car (cdr rest)) (vector-length vector))))
        (vector-sort! less? vector start end)
        (vector-ref vector (+ start k))))

    (define (vector-separate! less? vector k . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (car (cdr rest)) (vector-length vector))))
        (vector-sort! less? vector start end)
        (if #f #f)))

;;;; SRFI 95 generic procedures

    (define (sorted? sequence less? . rest)
      (let ((key (if (pair? rest) (car rest) #f)))
        (let ((items (cond ((list? sequence) sequence)
                           ((vector? sequence) (vector->list sequence))
                           ((string? sequence) (string->list sequence))
                           (else (error "sorted?: expected a sequence"
                                        sequence)))))
          (let ((key-of (key-cache key)))
            (let loop ((items items))
              (cond ((null? items) #t)
                    ((null? (cdr items)) #t)
                    ((less? (key-of (car (cdr items)))
                            (key-of (car items)))
                     #f)
                    (else (loop (cdr items)))))))))

    (define (sort sequence less? . rest)
      (let ((key (if (pair? rest) (car rest) #f)))
        (cond ((list? sequence) (list-sort-range less? sequence key))
              ((vector? sequence)
               (list->vector (list-sort-range less? (vector->list sequence) key)))
              ((string? sequence)
               (list->string (list-sort-range less? (string->list sequence) key)))
              (else (error "sort: expected a sequence" sequence)))))

    (define (sort! sequence less? . rest)
      (let ((key (if (pair? rest) (car rest) #f)))
        (cond ((list? sequence)
               (let ((sorted (list-sort-range less? sequence key)))
                 (let loop ((target sequence) (source sorted))
                   (if (pair? target)
                       (begin
                         (set-car! target (car source))
                         (loop (cdr target) (cdr source)))))
                 sequence))
              ((vector? sequence)
               (let ((sorted (list-sort-range less? (vector->list sequence) key)))
                 (let loop ((i 0) (items sorted))
                   (if (pair? items)
                       (begin
                         (vector-set! sequence i (car items))
                         (loop (+ i 1) (cdr items)))))
                 sequence))
              ((string? sequence)
               (let ((sorted (list-sort-range less? (string->list sequence) key)))
                 (let loop ((i 0) (items sorted))
                   (if (pair? items)
                       (begin
                         (string-set! sequence i (car items))
                         (loop (+ i 1) (cdr items)))))
                 sequence))
              (else (error "sort!: expected a sequence" sequence)))))

    (define (merge left right less? . rest)
      (let ((key (if (pair? rest) (car rest) #f)))
        (values-of (merge-two less?
                              (decorate-list key left)
                              (decorate-list key right)))))

    (define (merge-keyed-lists! less? key-of left right)
      (cond ((null? left) right)
            ((null? right) left)
            ((less? (key-of (car right)) (key-of (car left)))
             (let ((next (cdr right)))
               (set-cdr! right (merge-keyed-lists! less? key-of left next))
               right))
            (else
             (let ((next (cdr left)))
               (set-cdr! left (merge-keyed-lists! less? key-of next right))
               left))))

    (define (merge! left right less? . rest)
      (merge-keyed-lists! less?
                          (key-cache (if (pair? rest) (car rest) #f))
                          left right))))
