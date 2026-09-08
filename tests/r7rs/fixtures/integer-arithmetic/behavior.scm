(define (report name value) (write (list name value)) (newline))
(define big 10000000000000000000000000000000000000000)
(define (read-number text)
  (let ((port (open-input-string text)))
    (let ((value (read port))) (close-input-port port) value)))
(define (caught thunk)
  (guard (condition (else 'caught)) (thunk) 'not-caught))
(define (integer-results)
  (report 'errors
    (list (caught (lambda () (+ 1 #f)))
          (caught (lambda () (- #t)))
          (caught (lambda () (* big 'x)))
          (caught (lambda () (< 1 #f big)))
          (caught (lambda () (number->string #f)))))
  (report 'literal
    (list big -10000000000000000000000000000000000000000 +42 -0
          2305843009213693951 -2305843009213693952))
  (report 'quoted
    (list '(10000000000000000000000000000000000000000 . -10000000000000000000000000000000000000000)
          '#(10000000000000000000000000000000000000000 -10000000000000000000000000000000000000000)))
  (report 'promote
    (list (+ 2305843009213693951 1) (- -2305843009213693952 1)
          (- -2305843009213693952) (* 2305843009213693951 2)))
  (report 'demote
    (list (- 2305843009213693952 1) (+ -2305843009213693953 1)
          (- big big) (+ big (- big)) (* big 0)))
  (report 'arithmetic
    (list (+) (*) (- 42) (+ big 1 2) (- big 1 2) (* big 2 3)
          (* 12345678901234567890 98765432109876543210)))
  (report 'comparison
    (list (= big (read-number (number->string big)))
          (eqv? big (read-number (number->string big)))
          (eqv? big (+ big 1)) (eqv? 42 42)
          (< (- big) 0 big) (<= big big (+ big 1))
          (> big 0 (- big)) (>= big big (- big))
          (= big big (+ big 1)) (number? big) (number? 'big)))
  (report 'source-read
    (list (read-number "+00042") (read-number "-0000")
          (= -2305843009213693952 (read-number "-2305843009213693952"))
          (= big (read-number "10000000000000000000000000000000000000000"))))
  (report 'generated
    (letrec ((loop (lambda (n value)
                     (if (= n 0) #t
                         (if (= (- (+ value 12345678901234567890) 12345678901234567890) value)
                             (if (= (* value 3) (+ value value value))
                                 (loop (- n 1) (* value 10)) #f)
                             #f)))))
      (loop 24 big))))
(define fixture-result (begin (integer-results) 0))
