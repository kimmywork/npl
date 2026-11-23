(define (report name value) (write (list name value)) (newline))
(define big 10000000000000000000000000000000000000000)
(define (read-number text)
  (let ((port (open-input-string text)))
    (let ((value (read port))) (close-input-port port) value)))
(define (caught thunk)
  (guard (condition (else 'caught)) (thunk) 'not-caught))
; Fixnum boundaries: native fixnums are 62-bit, so these straddle the inline
; add/sub/compare fast path and its overflow fallback.
(define fmax 2305843009213693951)
(define fmin -2305843009213693952)
(define (add x y) (+ x y))
(define (sub x y) (- x y))
(define (add3 x y z) (+ x y z))
(define (sub3 x y z) (- x y z))
(define (neg x) (- x))
(define (less x y) (< x y))
(define (chain x y z) (<= x y z))
(define (boundary-results)
  (report 'overflow
    (list (+ fmax 1) (+ 1 fmax) (- fmin 1) (- 0 fmin) (- fmin) (+ fmax fmax)
          (+ fmin fmin) (- fmax fmin) (- fmin fmax)
          (add fmax 1) (sub fmin 1) (neg fmin) (add fmin -1) (sub fmax -1)))
  (report 'no-overflow
    (list (+ fmax fmin) (- fmax fmax) (+ fmax -1) (- fmin -1) (+ fmax 0)
          (- fmin 0) (+ fmax) (- fmax) (add fmax 0) (sub fmin 0)))
  (report 'fold
    (list (+ fmax 1 -1) (- fmin 1 -1) (+ 1 fmax -2) (add3 fmax 1 -1)
          (sub3 fmin 1 -1) (add3 fmax fmax fmin) (sub3 fmax -1 1)))
  (report 'mixed-bignum
    (list (+ big fmax) (- fmax big) (add big fmin) (sub (+ fmax 1) 1)
          (- (- fmin 1) -1)))
  (report 'mixed-inexact
    (list (inexact? (+ fmax 1.0)) (= (+ fmax 1.0) 2305843009213693952.0)
          (inexact? (sub fmin 0.5)) (< fmax 1.0e30) (= 1 1.0) (add 1 1.5)))
  (report 'mixed-rational
    (list (+ fmax 1/2) (- fmin 1/2) (add 1/2 1) (sub 1 1/3)))
  (report 'compare-boundary
    (list (< fmin fmax) (<= fmax fmax) (>= fmin fmin) (> fmax fmin)
          (= fmin fmin) (< fmax (+ fmax 1)) (> fmin (- fmin 1))
          (< fmin (+ fmax 1) big) (less fmax (+ fmax 1)) (less fmax 1/2)
          (chain fmin fmax fmax) (chain fmax fmin 0) (= fmax (+ fmax 0.0))
          (< 1/2 1) (> fmax 5/2))))
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
(define fixture-result (begin (integer-results) (boundary-results) 0))
