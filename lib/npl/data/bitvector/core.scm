    (define-record-type bitvector-type
      (make-bitvector-record store length)
      bitvector?
      (store bitvector-store)
      (length bitvector-record-length))

    (define (bitvector-length bvec) (bitvector-record-length bvec))

    (define bits-per-byte 8)

    (define (check-bitvector who value)
      (if (bitvector? value)
          value
          (error who "expected a bitvector" value)))

    (define (check-index who bvec index)
      (if (if (exact-integer? index)
              (if (<= 0 index) (< index (bitvector-length bvec)) #f)
              #f)
          index
          (error who "index out of range" index)))

    (define (check-range who bvec start end)
      (if (if (exact-integer? start)
              (if (exact-integer? end)
                  (if (<= 0 start end) (<= end (bitvector-length bvec)) #f)
                  #f)
              #f)
          (cons start end)
          (error who "invalid range" start end)))

    (define (range-start range) (car range))
    (define (range-end range) (cdr range))

    (define (check-bit who value)
      (cond ((eq? value #t) 1)
            ((eq? value #f) 0)
            ((equal? value 0) 0)
            ((equal? value 1) 1)
            (else (error who "expected a bit" value))))

    (define (bit->integer bit) (check-bit "bit->integer" bit))
    (define (bit->boolean bit) (= 1 (check-bit "bit->boolean" bit)))

    (define (byte-count length)
      (quotient (+ length 7) bits-per-byte))

    (define (make-empty store length)
      (make-bitvector-record store length))

    (define (allocate length)
      (make-empty (make-bytevector (byte-count length) 0) length))

    (define (bit-byte-index bvec index)
      (quotient index bits-per-byte))

    (define (bit-within-byte bvec index)
      (remainder index bits-per-byte))

    (define (bit-mask bvec index)
      (let loop ((remaining (bit-within-byte bvec index)) (mask 1))
        (if (= remaining 0) mask (loop (- remaining 1) (* mask 2)))))

    (define (bitvector-ref/int bvec index)
      (check-bitvector "bitvector-ref/int" bvec)
      (check-index "bitvector-ref/int" bvec index)
      (let* ((store (bitvector-store bvec))
             (byte (bytevector-u8-ref store (bit-byte-index bvec index)))
             (mask (bit-mask bvec index)))
        (if (= 0 (remainder (quotient byte mask) 2)) 0 1)))

    (define (bitvector-ref/bool bvec index)
      (= 1 (bitvector-ref/int bvec index)))

    (define (bitvector-set! bvec index bit)
      (check-bitvector "bitvector-set!" bvec)
      (check-index "bitvector-set!" bvec index)
      (let* ((store (bitvector-store bvec))
             (byte-index (bit-byte-index bvec index))
             (mask (bit-mask bvec index))
             (byte (bytevector-u8-ref store byte-index))
             (without (remainder byte mask))
             (higher (* (* mask 2) (quotient byte (* mask 2)))))
        (bytevector-u8-set! store byte-index
          (if (= 1 (check-bit "bitvector-set!" bit))
              (+ without mask higher)
              (+ without higher)))))

    (define (copy-into! target target-start source source-start count)
      (let loop ((offset 0))
        (if (< offset count)
            (begin
              (bitvector-set! target (+ target-start offset)
                              (bitvector-ref/int source (+ source-start offset)))
              (loop (+ offset 1))))))

    (define (fresh-copy bvec start end)
      (let* ((length (- end start))
             (result (allocate length)))
        (copy-into! result 0 bvec start length)
        result))

    (define (make-bitvector size . rest)
      (let ((length (if (exact-integer? size)
                        size
                        (error "make-bitvector" "expected a size" size)))
            (bit (if (pair? rest) (check-bit "make-bitvector" (car rest)) 0)))
        (if (< length 0) (error "make-bitvector" "negative size" length))
        (let ((result (allocate length)))
          (if (= bit 1)
              (let loop ((index 0))
                (if (< index length)
                    (begin (bitvector-set! result index 1)
                           (loop (+ index 1))))))
          result)))

    (define (bitvector . bits)
      (let ((result (allocate (length bits))))
        (let loop ((index 0) (items bits))
          (if (pair? items)
              (begin
                (bitvector-set! result index (check-bit "bitvector" (car items)))
                (loop (+ index 1) (cdr items)))))
        result))

    (define (bitvector-unfold mapper length . seeds)
      (let ((result (allocate length)))
        (let loop ((index 0) (state seeds))
          (if (< index length)
              (call-with-values
                (lambda () (apply mapper index state))
                (lambda (bit . next)
                  (bitvector-set! result index (check-bit "bitvector-unfold" bit))
                  (loop (+ index 1) next)))
              result))))

    (define (bitvector-unfold-right mapper length . seeds)
      (let ((result (allocate length)))
        (let loop ((index (- length 1)) (state seeds))
          (if (>= index 0)
              (call-with-values
                (lambda () (apply mapper index state))
                (lambda (bit . next)
                  (bitvector-set! result index
                                  (check-bit "bitvector-unfold-right" bit))
                  (loop (- index 1) next)))
              result))))

    (define (bitvector-copy bvec . rest)
      (check-bitvector "bitvector-copy" bvec)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (bitvector-length bvec)))
             (range (check-range "bitvector-copy" bvec start end)))
        (fresh-copy bvec (range-start range) (range-end range))))

    (define (bitvector-reverse-copy bvec . rest)
      (let ((copy (apply bitvector-copy bvec rest)))
        (bitvector-reverse! copy)
        copy))

    (define (bitvector-append . vectors)
      (let* ((length (let loop ((items vectors) (total 0))
                       (if (null? items)
                           total
                           (loop (cdr items)
                                 (+ total
                                    (bitvector-length (car items)))))))
             (result (allocate length)))
        (let loop ((index 0) (items vectors))
          (if (pair? items)
              (let ((bvec (car items)))
                (copy-into! result index bvec 0 (bitvector-length bvec))
                (loop (+ index (bitvector-length bvec)) (cdr items)))))
        result))

    (define (bitvector-concatenate vectors)
      (apply bitvector-append vectors))

    (define (bitvector-append-subbitvectors . arguments)
      (let loop ((items arguments) (pieces '()))
        (if (null? items)
            (apply bitvector-append (reverse pieces))
            (if (if (pair? (cdr items)) (pair? (cdr (cdr items))) #f)
                (loop (cdr (cdr (cdr items)))
                      (cons (bitvector-copy (car items)
                                            (car (cdr items))
                                            (car (cdr (cdr items))))
                            pieces))
                (error "bitvector-append-subbitvectors"
                       "expected bitvector, start, end triplets")))))

    (define (bitvector-empty? bvec)
      (check-bitvector "bitvector-empty?" bvec)
      (zero? (bitvector-length bvec)))

    (define (bitvector=? . vectors)
      (cond ((null? vectors) #t)
            ((null? (cdr vectors)) #t)
            (else
             (let ((left (car vectors))
                   (right (car (cdr vectors))))
               (if (not (= (bitvector-length left) (bitvector-length right)))
                   #f
                   (let loop ((index 0))
                     (cond ((= index (bitvector-length left))
                            (apply bitvector=? (cdr vectors)))
                           ((= (bitvector-ref/int left index)
                               (bitvector-ref/int right index))
                            (loop (+ index 1)))
                           (else #f))))))))

    (define (bitvector-take bvec count)
      (check-bitvector "bitvector-take" bvec)
      (if (if (exact-integer? count)
              (if (<= 0 count) (<= count (bitvector-length bvec)) #f)
              #f)
          (bitvector-copy bvec 0 count)
          (error "bitvector-take" "invalid count" count)))

    (define (bitvector-take-right bvec count)
      (check-bitvector "bitvector-take-right" bvec)
      (if (if (exact-integer? count)
              (if (<= 0 count) (<= count (bitvector-length bvec)) #f)
              #f)
          (bitvector-copy bvec (- (bitvector-length bvec) count)
                          (bitvector-length bvec))
          (error "bitvector-take-right" "invalid count" count)))

    (define (bitvector-drop bvec count)
      (bitvector-copy bvec count (bitvector-length bvec)))

    (define (bitvector-drop-right bvec count)
      (bitvector-copy bvec 0 (- (bitvector-length bvec) count)))

    (define (bitvector-segment bvec size)
      (if (if (exact-integer? size) (positive? size) #f)
          #f
          (error "bitvector-segment" "positive size required" size))
      (let loop ((start 0) (result '()))
        (if (>= start (bitvector-length bvec))
            (reverse result)
            (let ((end (min (+ start size) (bitvector-length bvec))))
              (loop end (cons (bitvector-copy bvec start end) result))))))

    (define (elements-at bvecs index)
      (if (null? bvecs)
          '()
          (cons (bitvector-ref/int (car bvecs) index)
                (elements-at (cdr bvecs) index))))

    (define (bitvector-fold/int kons knil bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0) (state knil))
          (if (>= index (bitvector-length bvec))
              state
              (loop (+ index 1)
                    (apply kons state (elements-at bvecs index)))))))

    (define (bitvector-fold/bool kons knil bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0) (state knil))
          (if (>= index (bitvector-length bvec))
              state
              (loop (+ index 1)
                    (apply kons state
                           (map (lambda (value) (= value 1))
                                (elements-at bvecs index))))))))

    (define (bitvector-fold-right/int kons knil bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index (- (bitvector-length bvec) 1)) (state knil))
          (if (< index 0)
              state
              (loop (- index 1)
                    (apply kons state (elements-at bvecs index)))))))

    (define (bitvector-fold-right/bool kons knil bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index (- (bitvector-length bvec) 1)) (state knil))
          (if (< index 0)
              state
              (loop (- index 1)
                    (apply kons state
                           (map (lambda (value) (= value 1))
                                (elements-at bvecs index))))))))

    (define (bitvector-map/int mapper bvec . rest)
      (let* ((bvecs (cons bvec rest))
             (result (allocate (bitvector-length bvec))))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (begin
                (bitvector-set! result index
                  (check-bit "bitvector-map/int"
                             (apply mapper (elements-at bvecs index))))
                (loop (+ index 1)))))
        result))

    (define (bitvector-map/bool mapper bvec . rest)
      (let* ((bvecs (cons bvec rest))
             (result (allocate (bitvector-length bvec))))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (begin
                (bitvector-set! result index
                  (check-bit "bitvector-map/bool"
                             (apply mapper
                                    (map (lambda (value) (= value 1))
                                         (elements-at bvecs index)))))
                (loop (+ index 1)))))
        result))

    (define (bitvector-map!/int mapper bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (begin
                (bitvector-set! bvec index
                  (check-bit "bitvector-map!/int"
                             (apply mapper (elements-at bvecs index))))
                (loop (+ index 1)))))))

    (define (bitvector-map!/bool mapper bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (begin
                (bitvector-set! bvec index
                  (check-bit "bitvector-map!/bool"
                             (apply mapper
                                    (map (lambda (value) (= value 1))
                                         (elements-at bvecs index)))))
                (loop (+ index 1)))))))

    (define (bitvector-map->list/int mapper bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0) (result '()))
          (if (>= index (bitvector-length bvec))
              (reverse result)
              (loop (+ index 1)
                    (cons (apply mapper (elements-at bvecs index)) result))))))

    (define (bitvector-map->list/bool mapper bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0) (result '()))
          (if (>= index (bitvector-length bvec))
              (reverse result)
              (loop (+ index 1)
                    (cons (apply mapper
                                 (map (lambda (value) (= value 1))
                                      (elements-at bvecs index)))
                          result))))))

    (define (bitvector-for-each/int proc bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (begin (apply proc (elements-at bvecs index))
                     (loop (+ index 1)))))))

    (define (bitvector-for-each/bool proc bvec . rest)
      (let ((bvecs (cons bvec rest)))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (begin (apply proc
                            (map (lambda (value) (= value 1))
                                 (elements-at bvecs index)))
                     (loop (+ index 1)))))))

    (define (bitvector-prefix-length left right)
      (let loop ((index 0))
        (if (if (< index (bitvector-length left))
                (< index (bitvector-length right))
                #f)
            (if (= (bitvector-ref/int left index)
                   (bitvector-ref/int right index))
                (loop (+ index 1))
                index)
            index)))

    (define (bitvector-suffix-length left right)
      (let loop ((offset 0))
        (if (if (< offset (bitvector-length left))
                (< offset (bitvector-length right))
                #f)
            (if (= (bitvector-ref/int left
                                      (- (bitvector-length left) offset 1))
                   (bitvector-ref/int right
                                      (- (bitvector-length right) offset 1)))
                (loop (+ offset 1))
                offset)
            offset)))

    (define (bitvector-prefix? prefix bvec)
      (if (> (bitvector-length prefix) (bitvector-length bvec))
          #f
          (= (bitvector-prefix-length prefix bvec) (bitvector-length prefix))))

    (define (bitvector-suffix? suffix bvec)
      (if (> (bitvector-length suffix) (bitvector-length bvec))
          #f
          (= (bitvector-suffix-length suffix bvec) (bitvector-length suffix))))

    (define (bitvector-pad bit bvec length)
      (let ((bit (check-bit "bitvector-pad" bit)))
        (if (<= length (bitvector-length bvec))
            (bitvector-copy bvec 0 length)
            (let ((result (make-bitvector length bit)))
              (copy-into! result (- length (bitvector-length bvec)) bvec 0
                          (bitvector-length bvec))
              result))))

    (define (bitvector-pad-right bit bvec length)
      (let ((bit (check-bit "bitvector-pad-right" bit)))
        (if (<= length (bitvector-length bvec))
            (bitvector-copy bvec 0 length)
            (let ((result (make-bitvector length bit)))
              (copy-into! result 0 bvec 0 (bitvector-length bvec))
              result))))

    (define (bitvector-trim bit bvec)
      (let ((bit (check-bit "bitvector-trim" bit)))
        (let loop ((start 0))
          (if (if (< start (bitvector-length bvec))
                  (= (bitvector-ref/int bvec start) bit)
                  #f)
              (loop (+ start 1))
              (bitvector-copy bvec start (bitvector-length bvec))))))

    (define (bitvector-trim-right bit bvec)
      (let ((bit (check-bit "bitvector-trim-right" bit)))
        (let loop ((end (bitvector-length bvec)))
          (if (if (> end 0) (= (bitvector-ref/int bvec (- end 1)) bit) #f)
              (loop (- end 1))
              (bitvector-copy bvec 0 end)))))

    (define (bitvector-trim-both bit bvec)
      (bitvector-trim bit (bitvector-trim-right bit bvec)))

    (define (bitvector-swap! bvec left right)
      (let ((left-bit (bitvector-ref/int bvec left)))
        (bitvector-set! bvec left (bitvector-ref/int bvec right))
        (bitvector-set! bvec right left-bit)))

    (define (bitvector-reverse! bvec . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (bitvector-length bvec)))
             (range (check-range "bitvector-reverse!" bvec start end)))
        (let loop ((left (range-start range)) (right (- (range-end range) 1)))
          (if (< left right)
              (begin
                (bitvector-swap! bvec left right)
                (loop (+ left 1) (- right 1)))))))

    (define (bitvector-copy! target at source . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (bitvector-length source)))
             (range (check-range "bitvector-copy!" source start end))
             (snapshot (bitvector-copy source (range-start range)
                                       (range-end range))))
        (copy-into! target at snapshot 0 (bitvector-length snapshot))))

    (define (bitvector-reverse-copy! target at source . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (bitvector-length source)))
             (range (check-range "bitvector-reverse-copy!" source start end))
             (snapshot (bitvector-copy source (range-start range)
                                       (range-end range))))
        (let loop ((index 0) (target-index at))
          (if (< index (bitvector-length snapshot))
              (begin
                (bitvector-set! target target-index
                                (bitvector-ref/int
                                 snapshot (- (bitvector-length snapshot) 1 index)))
                (loop (+ index 1) (+ target-index 1)))))))

    (define (bitvector->list/int bvec . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (bitvector-length bvec)))
             (range (check-range "bitvector->list/int" bvec start end)))
        (let loop ((index (range-start range)) (result '()))
          (if (>= index (range-end range))
              (reverse result)
              (loop (+ index 1)
                    (cons (bitvector-ref/int bvec index) result))))))

    (define (bitvector->list/bool bvec . rest)
      (map (lambda (bit) (= bit 1)) (apply bitvector->list/int bvec rest)))

    (define (reverse-range->list bvec start end)
      (reverse (bitvector->list/int bvec start end)))

    (define (reverse-bitvector->list/int bvec . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (bitvector-length bvec)))
             (range (check-range "reverse-bitvector->list/int" bvec start end)))
        (reverse-range->list bvec (range-start range) (range-end range))))

    (define (reverse-bitvector->list/bool bvec . rest)
      (map (lambda (bit) (= bit 1))
           (apply reverse-bitvector->list/int bvec rest)))

    (define (list->bitvector bits)
      (apply bitvector bits))

    (define (reverse-list->bitvector bits)
      (apply bitvector (reverse bits)))

    (define (bits->vector-conversion who bits)
      (let ((result (make-vector (length bits))))
        (let loop ((index 0) (items bits))
          (if (pair? items)
              (begin
                (vector-set! result index
                             (if (eq? who 'bool) (= (car items) 1) (car items)))
                (loop (+ index 1) (cdr items)))))
        result))

    (define (bitvector->vector/int bvec . rest)
      (bits->vector-conversion 'int (apply bitvector->list/int bvec rest)))

    (define (bitvector->vector/bool bvec . rest)
      (bits->vector-conversion 'bool (apply bitvector->list/int bvec rest)))

    (define (reverse-bitvector->vector/int bvec . rest)
      (bits->vector-conversion 'int (apply reverse-bitvector->list/int bvec rest)))

    (define (reverse-bitvector->vector/bool bvec . rest)
      (bits->vector-conversion 'bool (apply reverse-bitvector->list/int bvec rest)))

    (define (vector->bitvector vector . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (vector-length vector)))
             (result (allocate (- end start))))
        (let loop ((offset 0))
          (if (< offset (- end start))
              (begin
                (bitvector-set! result offset
                                (check-bit "vector->bitvector"
                                           (vector-ref vector (+ start offset))))
                (loop (+ offset 1)))))
        result))

    (define (reverse-vector->bitvector vector . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (vector-length vector)))
             (result (allocate (- end start))))
        (let loop ((offset 0))
          (if (< offset (- end start))
              (begin
                (bitvector-set! result offset
                                (check-bit "reverse-vector->bitvector"
                                           (vector-ref vector
                                                       (- end 1 offset))))
                (loop (+ offset 1)))))
        result))

    (define (bitvector->string bvec)
      (let loop ((index 0) (result "#*"))
        (if (>= index (bitvector-length bvec))
            result
            (loop (+ index 1)
                  (string-append result
                                 (if (= 1 (bitvector-ref/int bvec index))
                                     "1" "0"))))))

    (define (string->bitvector string)
      (if (if (>= (string-length string) 2)
              (string=? (substring string 0 2) "#*")
              #f)
          (let ((result (make-bitvector (- (string-length string) 2))))
            (let loop ((index 2))
              (if (>= index (string-length string))
                  result
                  (let ((char (string-ref string index)))
                    (cond ((char=? char #\0)
                           (loop (+ index 1)))
                          ((char=? char #\1)
                           (bitvector-set! result (- index 2) 1)
                           (loop (+ index 1)))
                          (else #f))))))
          #f))

    (define (bitvector->integer bvec)
      (let loop ((index 0)
                 (result 0)
                 (power 1))
        (if (>= index (bitvector-length bvec))
            result
            (loop (+ index 1)
                  (+ result
                     (if (= 1 (bitvector-ref/int bvec index)) power 0))
                  (* power 2)))))

    (define (bitvector-integer-length integer)
      (let loop ((value integer) (length 0))
        (if (zero? value) length (loop (quotient value 2) (+ length 1)))))

    (define (integer->bitvector integer . rest)
      (let* ((length (if (pair? rest)
                         (car rest)
                         (bitvector-integer-length integer)))
             (result (make-bitvector length)))
        (let loop ((value integer) (index 0))
          (if (if (positive? value) (< index length) #f)
              (begin
                (if (= 1 (remainder value 2))
                    (bitvector-set! result index 1))
                (loop (quotient value 2) (+ index 1)))))
        result))

    (define (make-bitvector/int-generator bvec)
      (let ((index 0))
        (make-coroutine-generator
         (lambda (yield)
           (let loop ()
             (if (< index (bitvector-length bvec))
                 (begin (yield (bitvector-ref/int bvec index))
                        (set! index (+ index 1))
                        (loop))))))))

    (define (make-bitvector/bool-generator bvec)
      (let ((index 0))
        (make-coroutine-generator
         (lambda (yield)
           (let loop ()
             (if (< index (bitvector-length bvec))
                 (begin (yield (bitvector-ref/bool bvec index))
                        (set! index (+ index 1))
                        (loop))))))))

    (define (make-bitvector-accumulator)
      (let ((bits '()))
        (lambda (value)
          (if (eof-object? value)
              (apply bitvector (reverse bits))
              (begin
                (set! bits (cons (check-bit "make-bitvector-accumulator"
                                            value)
                                 bits))
                (if #f #f))))))
