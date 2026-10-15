; Bignum multiplication and decimal output: (fact 9000), the user-reported case.
(define (fact n) (if (= 0 n) 1 (* n (fact (- n 1)))))
(define (bench-main) (display (string-length (number->string (fact 9000)))) (newline))
