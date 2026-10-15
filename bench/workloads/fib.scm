; Non-tail procedure calls and fixnum arithmetic.
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(define (bench-main) (display (fib 30)) (newline))
