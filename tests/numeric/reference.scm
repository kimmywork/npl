(import (scheme base) (scheme write) (scheme process-context))

; Generate the same deterministic integer cases for the C substrate and Guile.
(define input? (string=? (cadr (command-line)) "input"))
(define (emit name operation a b)
  (if input?
      (begin (display name) (display " ") (display a)
             (display " ") (display b) (newline))
      (begin (write (operation a b)) (newline))))
(define (check-pair a b)
  (emit "quotient" truncate-quotient a b)
  (emit "remainder" truncate-remainder a b)
  (emit "gcd" gcd a b)
  (emit "divide" / a b)
  (if (not (= a 0))
      (begin
        (emit "fraction-add" (lambda (a b) (+ (/ a b) (/ b a))) a b)
        (emit "fraction-sub" (lambda (a b) (- (/ a b) (/ b a))) a b)
        (emit "fraction-mul" (lambda (a b) (* (/ a b) (/ b a))) a b)
        (emit "fraction-div" (lambda (a b) (/ (/ a b) (/ b a))) a b)
        (emit "fraction-cmp"
              (lambda (a b) (cond ((< (/ a b) (/ b a)) -1)
                                  ((> (/ a b) (/ b a)) 1) (else 0))) a b))))
(define seed 12345)
(define (next)
  (set! seed (modulo (+ (* seed 1664525) 1013904223) 4294967296))
  seed)
(define (integer-digits length)
  (let loop ((n length) (value 1))
    (if (= n 0) value
        (loop (- n 1) (+ (* value 10) (modulo (next) 10))))))
(let outer ((as '(0 1 -1 17 -17 2305843009213693951 -2305843009213693952
                 999999999999999999999999999999999999999999999999999999)))
  (if (pair? as)
      (begin
        (let inner ((bs '(1 -1 2 -2 5 -5 999999999 -999999999
                          1000000000 -1000000000 2305843009213693952
                          -2305843009213693952)))
          (if (pair? bs)
              (begin (check-pair (car as) (car bs)) (inner (cdr bs)))))
        (outer (cdr as)))))
(do ((i 0 (+ i 1))) ((= i 160))
  (let* ((a (integer-digits (+ 20 (modulo (* i 17) 280))))
         (b (integer-digits (+ 1 (modulo (* i 13) 160)))))
    (check-pair (if (even? i) a (- a))
                (if (< (modulo i 4) 2) b (- b)))))
