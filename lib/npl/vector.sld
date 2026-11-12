; SRFI 133 vectors with the canonical NPL library name.
;
; The R7RS vector procedures whose signatures match SRFI 133 are re-exported
; from (scheme base) with their original identity. vector-copy is the SRFI
; extension with an optional fill value, so it is defined here.
(define-library (npl vector)
  (export
    make-vector vector vector-unfold vector-unfold-right
    vector-copy vector-reverse-copy
    vector-append vector-concatenate vector-append-subvectors
    vector? vector-empty? vector=
    vector-ref vector-length
    vector-fold vector-fold-right
    vector-map vector-map! vector-for-each vector-count vector-cumulate
    vector-index vector-index-right vector-skip vector-skip-right
    vector-binary-search vector-any vector-every vector-partition
    vector-set! vector-swap! vector-fill! vector-reverse!
    vector-copy! vector-reverse-copy!
    vector-unfold! vector-unfold-right!
    vector->list reverse-vector->list
    list->vector reverse-list->vector
    vector->string string->vector)
  (import (scheme base) (scheme cxr))
  (begin
    (define (vector-unfold f length . seeds)
      (let ((result (make-vector length)))
        (let loop ((i 0) (seeds seeds))
          (if (< i length)
              (call-with-values
               (lambda () (apply f i seeds))
               (lambda (value . next-seeds)
                 (vector-set! result i value)
                 (loop (+ i 1) next-seeds)))))
        result))

    (define (vector-unfold-right f length . seeds)
      (let ((result (make-vector length)))
        (let loop ((i (- length 1)) (seeds seeds))
          (if (>= i 0)
              (call-with-values
               (lambda () (apply f i seeds))
               (lambda (value . next-seeds)
                 (vector-set! result i value)
                 (loop (- i 1) next-seeds)))))
        result))

    (define (vector-reverse-copy vec . rest)
      (let* ((len (vector-length vec))
             (start (if (pair? rest) (car rest) 0))
             (rest (if (pair? rest) (cdr rest) '()))
             (end (if (pair? rest) (car rest) len))
             (size (- end start))
             (result (make-vector size)))
        (let loop ((i 0))
          (if (< i size)
              (begin
                (vector-set! result i (vector-ref vec (- end 1 i)))
                (loop (+ i 1)))))
        result))

    (define (vector-concatenate vectors)
      (let ((total (let loop ((items vectors) (n 0))
                     (if (pair? items)
                         (loop (cdr items) (+ n (vector-length (car items))))
                         n))))
        (let ((result (make-vector total)))
          (let loop ((items vectors) (at 0))
            (if (pair? items)
                (let ((item (car items)))
                  (let copy ((i 0))
                    (if (< i (vector-length item))
                        (begin
                          (vector-set! result (+ at i) (vector-ref item i))
                          (copy (+ i 1)))))
                  (loop (cdr items) (+ at (vector-length item))))))
          result)))

    (define (vector-append-subvectors . args)
      (let loop ((rest args) (pieces '()))
        (if (null? rest)
            (vector-concatenate (reverse pieces))
            (loop (cdddr rest)
                  (cons (vector-copy (car rest) (cadr rest) (caddr rest))
                        pieces)))))

    (define (vector-empty? vec) (= 0 (vector-length vec)))

    (define (vector= elt=? . vectors)
      (let loop ((items vectors))
        (if (or (null? items) (null? (cdr items)))
            #t
            (let ((a (car items)) (b (cadr items)))
              (if (not (= (vector-length a) (vector-length b)))
                  #f
                  (let compare ((i 0))
                    (cond ((= i (vector-length a)) (loop (cdr items)))
                          ((elt=? (vector-ref a i) (vector-ref b i))
                           (compare (+ i 1)))
                          (else #f))))))))

    (define (%shortest vectors)
      (let loop ((items vectors) (n #f))
        (if (pair? items)
            (let ((len (vector-length (car items))))
              (loop (cdr items) (if n (min n len) len)))
            n)))

    (define (vector-fold kons knil . vectors)
      (let ((len (%shortest vectors)))
        (let loop ((i 0) (state knil))
          (if (< i len)
              (loop (+ i 1)
                    (apply kons state
                           (map (lambda (v) (vector-ref v i)) vectors)))
              state))))

    (define (vector-fold-right kons knil . vectors)
      (let ((len (%shortest vectors)))
        (let loop ((i (- len 1)) (state knil))
          (if (>= i 0)
              (loop (- i 1)
                    (apply kons state
                           (map (lambda (v) (vector-ref v i)) vectors)))
              state))))

    (define (vector-map! proc vec . rest)
      (let ((len (vector-length vec)))
        (let loop ((i 0))
          (if (< i len)
              (begin
                (vector-set! vec i
                             (apply proc
                                    (vector-ref vec i)
                                    (map (lambda (v) (vector-ref v i)) rest)))
                (loop (+ i 1)))))
        vec))

    (define (vector-count pred . vectors)
      (let ((len (%shortest vectors)))
        (let loop ((i 0) (n 0))
          (if (< i len)
              (loop (+ i 1)
                    (if (apply pred
                               (map (lambda (v) (vector-ref v i)) vectors))
                        (+ n 1)
                        n))
              n))))

    (define (vector-cumulate f knil vec)
      (let* ((len (vector-length vec))
             (result (make-vector len)))
        (let loop ((i 0) (previous knil))
          (if (< i len)
              (let ((value (f previous (vector-ref vec i))))
                (vector-set! result i value)
                (loop (+ i 1) value))))
        result))

    (define (%vector-index who pred vectors right)
      (let ((len (%shortest vectors)))
        (let loop ((i (if right (- len 1) 0)))
          (if (if right (< i 0) (>= i len))
              #f
              (if (apply pred
                         (map (lambda (v) (vector-ref v i)) vectors))
                  i
                  (loop (if right (- i 1) (+ i 1))))))))

    (define (vector-index pred . vectors)
      (%vector-index "vector-index" pred vectors #f))

    (define (vector-index-right pred . vectors)
      (%vector-index "vector-index-right" pred vectors #t))

    (define (%vector-skip who pred vectors right)
      (let ((len (%shortest vectors)))
        (let loop ((i (if right (- len 1) 0)))
          (if (if right (< i 0) (>= i len))
              #f
              (if (apply pred
                         (map (lambda (v) (vector-ref v i)) vectors))
                  (loop (if right (- i 1) (+ i 1)))
                  i)))))

    (define (vector-skip pred . vectors)
      (%vector-skip "vector-skip" pred vectors #f))

    (define (vector-skip-right pred . vectors)
      (%vector-skip "vector-skip-right" pred vectors #t))

    (define (vector-binary-search vec value cmp)
      (let loop ((low 0) (high (- (vector-length vec) 1)))
        (if (> low high)
            #f
            (let* ((mid (quotient (+ low high) 2))
                   (order (cmp (vector-ref vec mid) value)))
              (cond ((= order 0) mid)
                    ((< order 0) (loop (+ mid 1) high))
                    (else (loop low (- mid 1))))))))

    (define (vector-any pred . vectors)
      (let ((len (%shortest vectors)))
        (let loop ((i 0))
          (if (>= i len)
              #f
              (let ((value (apply pred
                                  (map (lambda (v) (vector-ref v i)) vectors))))
                (if value value (loop (+ i 1))))))))

    (define (vector-every pred . vectors)
      (let ((len (%shortest vectors)))
        (let loop ((i 0) (result #t))
          (if (>= i len)
              result
              (let ((value (apply pred
                                  (map (lambda (v) (vector-ref v i)) vectors))))
                (if value (loop (+ i 1) value) #f))))))

    (define (vector-partition pred vec)
      (let loop ((i 0) (yes '()) (no '()))
        (if (< i (vector-length vec))
            (let ((value (vector-ref vec i)))
              (if (pred value)
                  (loop (+ i 1) (cons value yes) no)
                  (loop (+ i 1) yes (cons value no))))
            (let* ((yes (reverse yes))
                   (no (reverse no))
                   (result (make-vector (vector-length vec))))
              (let fill ((items (append yes no)) (at 0))
                (if (pair? items)
                    (begin
                      (vector-set! result at (car items))
                      (fill (cdr items) (+ at 1)))))
              (values result (length yes))))))

    (define (vector-swap! vec i j)
      (let ((value (vector-ref vec i)))
        (vector-set! vec i (vector-ref vec j))
        (vector-set! vec j value)))

    (define (vector-reverse! vec . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (cadr rest)
                      (vector-length vec))))
        (let loop ((i start) (j (- end 1)))
          (if (< i j)
              (begin
                (vector-swap! vec i j)
                (loop (+ i 1) (- j 1)))))
        vec))

    (define (vector-reverse-copy! target tstart source . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (cadr rest)
                      (vector-length source)))
             (count (- end start))
             (temporary (make-vector count)))
        ;; Copy through a temporary so overlapping target/source ranges work.
        (let loop ((i 0))
          (if (< i count)
              (begin
                (vector-set! temporary i (vector-ref source (- end 1 i)))
                (loop (+ i 1)))))
        (let loop ((i 0))
          (if (< i count)
              (begin
                (vector-set! target (+ tstart i) (vector-ref temporary i))
                (loop (+ i 1)))))
        target))

    (define (vector-unfold! f vec start end . seeds)
      (let loop ((i start) (seeds seeds))
        (if (< i end)
            (call-with-values
             (lambda () (apply f i seeds))
             (lambda (value . next-seeds)
               (vector-set! vec i value)
               (loop (+ i 1) next-seeds)))))
      vec)

    (define (vector-unfold-right! f vec start end . seeds)
      (let loop ((i (- end 1)) (seeds seeds))
        (if (>= i start)
            (call-with-values
             (lambda () (apply f i seeds))
             (lambda (value . next-seeds)
               (vector-set! vec i value)
               (loop (- i 1) next-seeds)))))
      vec)

    (define (reverse-vector->list vec . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (and (pair? rest) (pair? (cdr rest)))
                      (cadr rest)
                      (vector-length vec))))
        (let loop ((i start) (result '()))
          (if (< i end)
              (loop (+ i 1) (cons (vector-ref vec i) result))
              result))))

    (define (reverse-list->vector lst)
      (let* ((len (length lst))
             (result (make-vector len)))
        (let loop ((items lst) (i (- len 1)))
          (if (pair? items)
              (begin
                (vector-set! result i (car items))
                (loop (cdr items) (- i 1)))))
        result)))
)
