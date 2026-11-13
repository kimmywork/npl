    (define (bitvector-not bvec)
      (let ((result (allocate (bitvector-length bvec))))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (begin
                (bitvector-set! result index
                                (- 1 (bitvector-ref/int bvec index)))
                (loop (+ index 1)))))
        result))

    (define (bitvector-not! bvec)
      (let loop ((index 0))
        (if (< index (bitvector-length bvec))
            (begin
              (bitvector-set! bvec index
                              (- 1 (bitvector-ref/int bvec index)))
              (loop (+ index 1))))))

    (define (bitvector-binary who op left right mutating)
      (check-bitvector who left)
      (check-bitvector who right)
      (let ((result (if mutating left (allocate (bitvector-length left)))))
        (let loop ((index 0))
          (if (< index (bitvector-length left))
              (begin
                (bitvector-set! result index
                  (op (bitvector-ref/int left index)
                      (bitvector-ref/int right index)))
                (loop (+ index 1)))))
        result))

    (define (and-op left right) (if (= left 1) right 0))
    (define (ior-op left right) (if (= left 1) 1 right))
    (define (xor-op left right) (if (= left right) 0 1))
    (define (eqv-op left right) (if (= left right) 1 0))
    (define (nand-op left right) (- 1 (and-op left right)))
    (define (nor-op left right) (- 1 (ior-op left right)))
    (define (andc1-op left right) (if (= left 0) 1 (- 1 right)))
    (define (andc2-op left right) (if (= right 1) 1 (- 1 left)))
    (define (orc1-op left right) (if (= left 1) 1 (- 1 right)))
    (define (orc2-op left right) (if (= right 0) 1 (- 1 left)))

    (define (bitvector-and left right) (bitvector-binary "bitvector-and" and-op left right #f))
    (define (bitvector-and! left right) (bitvector-binary "bitvector-and!" and-op left right #t))
    (define (bitvector-ior left right) (bitvector-binary "bitvector-ior" ior-op left right #f))
    (define (bitvector-ior! left right) (bitvector-binary "bitvector-ior!" ior-op left right #t))
    (define (bitvector-xor left right) (bitvector-binary "bitvector-xor" xor-op left right #f))
    (define (bitvector-xor! left right) (bitvector-binary "bitvector-xor!" xor-op left right #t))
    (define (bitvector-eqv left right) (bitvector-binary "bitvector-eqv" eqv-op left right #f))
    (define (bitvector-eqv! left right) (bitvector-binary "bitvector-eqv!" eqv-op left right #t))
    (define (bitvector-nand left right) (bitvector-binary "bitvector-nand" nand-op left right #f))
    (define (bitvector-nand! left right) (bitvector-binary "bitvector-nand!" nand-op left right #t))
    (define (bitvector-nor left right) (bitvector-binary "bitvector-nor" nor-op left right #f))
    (define (bitvector-nor! left right) (bitvector-binary "bitvector-nor!" nor-op left right #t))
    (define (bitvector-andc1 left right) (bitvector-binary "bitvector-andc1" andc1-op left right #f))
    (define (bitvector-andc1! left right) (bitvector-binary "bitvector-andc1!" andc1-op left right #t))
    (define (bitvector-andc2 left right) (bitvector-binary "bitvector-andc2" andc2-op left right #f))
    (define (bitvector-andc2! left right) (bitvector-binary "bitvector-andc2!" andc2-op left right #t))
    (define (bitvector-orc1 left right) (bitvector-binary "bitvector-orc1" orc1-op left right #f))
    (define (bitvector-orc1! left right) (bitvector-binary "bitvector-orc1!" orc1-op left right #t))
    (define (bitvector-orc2 left right) (bitvector-binary "bitvector-orc2" orc2-op left right #f))
    (define (bitvector-orc2! left right) (bitvector-binary "bitvector-orc2!" orc2-op left right #t))

    (define (bitvector-logical-shift bvec count bit)
      (let ((bit (check-bit "bitvector-logical-shift" bit))
            (result (allocate (bitvector-length bvec))))
        (let loop ((index 0))
          (if (< index (bitvector-length bvec))
              (let ((source-index (+ index count)))
                (bitvector-set! result index
                  (if (if (<= 0 source-index)
                          (< source-index (bitvector-length bvec))
                          #f)
                      (bitvector-ref/int bvec source-index)
                      bit))
                (loop (+ index 1)))))
        result))

    (define (bitvector-count bit bvec)
      (let ((bit (check-bit "bitvector-count" bit)))
        (let loop ((index 0) (total 0))
          (if (>= index (bitvector-length bvec))
              total
              (loop (+ index 1)
                    (if (= bit (bitvector-ref/int bvec index))
                        (+ total 1)
                        total))))))

    (define (bitvector-count-run bit bvec start)
      (let ((bit (check-bit "bitvector-count-run" bit)))
        (let loop ((index start) (total 0))
          (if (if (< index (bitvector-length bvec))
                  (= bit (bitvector-ref/int bvec index))
                  #f)
              (loop (+ index 1) (+ total 1))
              total))))

    (define (bitvector-if condition then else)
      (let ((result (allocate (bitvector-length condition))))
        (let loop ((index 0))
          (if (< index (bitvector-length condition))
              (begin
                (bitvector-set! result index
                  (if (= 1 (bitvector-ref/int condition index))
                      (bitvector-ref/int then index)
                      (bitvector-ref/int else index)))
                (loop (+ index 1)))))
        result))

    (define (bitvector-first-bit bit bvec)
      (let ((bit (check-bit "bitvector-first-bit" bit)))
        (let loop ((index 0))
          (cond ((>= index (bitvector-length bvec)) -1)
                ((= bit (bitvector-ref/int bvec index)) index)
                (else (loop (+ index 1)))))))

    (define (bitvector-field-any? bvec start end)
      (let* ((range (check-range "bitvector-field-any?" bvec start end)))
        (let loop ((index (range-start range)))
          (if (>= index (range-end range))
              #f
              (if (= 1 (bitvector-ref/int bvec index))
                  #t
                  (loop (+ index 1)))))))

    (define (bitvector-field-every? bvec start end)
      (let* ((range (check-range "bitvector-field-every?" bvec start end)))
        (let loop ((index (range-start range)))
          (if (>= index (range-end range))
              #t
              (if (= 0 (bitvector-ref/int bvec index))
                  #f
                  (loop (+ index 1)))))))

    (define (bitvector-field-clear bvec start end)
      (let ((result (bitvector-copy bvec)))
        (bitvector-field-clear! result start end)
        result))

    (define (bitvector-field-clear! bvec start end)
      (let* ((range (check-range "bitvector-field-clear!" bvec start end)))
        (let loop ((index (range-start range)))
          (if (< index (range-end range))
              (begin (bitvector-set! bvec index 0)
                     (loop (+ index 1)))))))

    (define (bitvector-field-set bvec start end)
      (let ((result (bitvector-copy bvec)))
        (bitvector-field-set! result start end)
        result))

    (define (bitvector-field-set! bvec start end)
      (let* ((range (check-range "bitvector-field-set!" bvec start end)))
        (let loop ((index (range-start range)))
          (if (< index (range-end range))
              (begin (bitvector-set! bvec index 1)
                     (loop (+ index 1)))))))

    (define (bitvector-field-replace dest source start end)
      (let ((result (bitvector-copy dest)))
        (bitvector-field-replace! result source start end)
        result))

    (define (bitvector-field-replace! dest source start end)
      (let loop ((offset 0))
        (if (< offset (- end start))
            (begin
              (bitvector-set! dest (+ start offset)
                              (bitvector-ref/int source offset))
              (loop (+ offset 1))))))

    (define (bitvector-field-replace-same dest source start end)
      (let ((result (bitvector-copy dest)))
        (bitvector-field-replace-same! result source start end)
        result))

    (define (bitvector-field-replace-same! dest source start end)
      (bitvector-copy! dest start source start end))

    (define (bitvector-field-rotate bvec count start end)
      (let* ((length (- end start))
             (source (bitvector-copy bvec start end))
             (result (bitvector-copy bvec)))
        (let loop ((offset 0))
          (if (< offset length)
              (begin
                (bitvector-set! result (+ start offset)
                                (bitvector-ref/int
                                 source (modulo (+ offset count) length)))
                (loop (+ offset 1)))))
        result))

    (define (bitvector-field-flip bvec start end)
      (let ((result (bitvector-copy bvec)))
        (bitvector-field-flip! result start end)
        result))

    (define (bitvector-field-flip! bvec start end)
      (let loop ((index start))
        (if (< index end)
            (begin
              (bitvector-set! bvec index
                              (- 1 (bitvector-ref/int bvec index)))
              (loop (+ index 1))))))
