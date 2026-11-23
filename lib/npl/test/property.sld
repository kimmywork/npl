; Property-based testing in the SRFI 252 style, on the (npl test) runner.
;
; Random inputs are deterministic through the property-seed parameter.  The
; generator constructors return ordinary thunks (SRFI 158 shape), so custom
; generators are just procedures; the built-ins cover R7RS-small atoms.
(define-library (npl test property)
  (export test-property test-property-expect-fail test-property-skip
          test-property-error test-property-error-type
          property-test-runner property-seed
          boolean-generator bytevector-generator char-generator
          exact-integer-generator integer-generator
          exact-real-generator real-generator number-generator
          rational-generator string-generator symbol-generator
          complex-generator)
  (import (scheme base)
          (scheme write)
          (npl test)
          (npl data generator))
  (begin
    (define property-seed (make-parameter 20261123))

    (define (next-random!)
      (let ((value (modulo (+ (* (property-seed) 1103515245) 12345)
                           2147483648)))
        (property-seed value)
        value))

    (define (random-below bound)
      (if (<= bound 0) 0 (modulo (next-random!) bound)))

    (define (random-integer)
      (- (random-below 2001) 1000))

    (define (random-real)
      (/ (random-below 1000000) 1000000.0))

    (define (banner-generator banner next)
      (let ((rest banner))
        (lambda ()
          (if (null? rest)
              (next)
              (let ((value (car rest)))
                (set! rest (cdr rest))
                value)))))

    (define (boolean-generator)
      (banner-generator '(#t #f)
                        (lambda () (if (= (random-below 2) 0) #t #f))))

    (define (exact-integer-generator)
      (banner-generator '(0 1 -1) random-integer))

    (define (integer-generator) (exact-integer-generator))

    (define (exact-real-generator)
      (banner-generator
        '(0 1 -1 1/2 -1/2)
        (lambda ()
          (/ (random-integer) (+ 1 (random-below 10))))))

    (define (real-generator)
      (banner-generator
        '(0 1 -1 1/2 -1/2 0.0 1.0 -1.0)
        (lambda ()
          (if (= (random-below 2) 0)
              (/ (random-integer) (+ 1 (random-below 10)))
              (random-real)))))

    (define (number-generator) (real-generator))
    (define (rational-generator) (real-generator))
    (define (complex-generator) (real-generator))

    (define (char-generator)
      (banner-generator
        (list #\null)
        (lambda () (integer->char (+ 32 (random-below 95))))))

    (define (string-generator)
      (banner-generator
        (list "")
        (lambda ()
          (let* ((length (random-below 20))
                 (result (make-string length #\space)))
            (let loop ((index 0))
              (if (= index length)
                  result
                  (begin
                    (string-set! result index
                                 (integer->char (+ 32 (random-below 95))))
                    (loop (+ index 1)))))))))

    (define (symbol-generator)
      (banner-generator
        (list (string->symbol ""))
        (lambda () (string->symbol (string-generator)))))

    (define (bytevector-generator)
      (banner-generator
        (list (bytevector))
        (lambda ()
          (let* ((length (random-below 32))
                 (result (make-bytevector length 0)))
            (let loop ((index 0))
              (if (= index length)
                  result
                  (begin
                    (bytevector-u8-set! result index (random-below 256))
                    (loop (+ index 1)))))))))

    (define (property-test-runner)
      (test-runner-simple))

    (define (show value)
      (let ((port (open-output-string)))
        (write value port)
        (get-output-string port)))

    (define (generator-values generators)
      (let loop ((rest generators) (values '()))
        (if (null? rest)
            (reverse values)
            (let ((value ((car rest))))
              (if (eof-object? value)
                  (error "property generator exhausted")
                  (loop (cdr rest) (cons value values)))))))

    (define (run-property property generators runs expect)
      (let loop ((index 0))
        (if (= index runs)
            #t
            (let ((values (generator-values generators))
                  (raised? #f)
                  (result #f))
              (guard (condition
                      (#t (set! raised? #t)
                          (if (eq? expect 'error)
                              #t
                              (begin
                                (test-record!
                                  'test-property #f
                                  (string-append
                                    "raised on " (show values)
                                    ": " (show condition)))
                                #f))))
                (set! result (apply property values)))
              (cond
                ((eq? expect 'error)
                 (if raised?
                     (loop (+ index 1))
                     (begin
                       (test-record! 'test-property-error #f
                                     (string-append "returned on "
                                                    (show values)))
                       #f)))
                ((eq? expect 'false)
                 (if result
                     (begin
                       (test-record! 'test-property-expect-fail #f
                                     (string-append "passed on "
                                                    (show values)))
                       #f)
                     (loop (+ index 1))))
                (else
                 (if raised?
                     #f
                     (if result
                         (loop (+ index 1))
                         (begin
                           (test-record! 'test-property #f
                                         (string-append "failed on "
                                                        (show values)))
                           #f)))))))))

    (define (test-property property generators . rest)
      (let ((runs (if (null? rest) 100 (car rest))))
        (if (run-property property generators runs 'true)
            (test-record! 'test-property #t "")
            #f)))

    (define (test-property-expect-fail property generators . rest)
      (let ((runs (if (null? rest) 100 (car rest))))
        (if (run-property property generators runs 'false)
            (test-record! 'test-property-expect-fail #t "")
            #f)))

    (define (test-property-skip property generators . rest)
      (test-record-skip! 'test-property-skip))

    (define (test-property-error property generators . rest)
      (let ((runs (if (null? rest) 100 (car rest))))
        (if (run-property property generators runs 'error)
            (test-record! 'test-property-error #t "")
            #f)))

    (define (test-property-error-type error-type property generators . rest)
      (test-property-error property generators
                           (if (null? rest) 100 (car rest))))))
