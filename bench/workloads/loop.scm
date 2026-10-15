; Tail loop with fixnum accumulation.
(define (loop i acc) (if (= i 0) acc (loop (- i 1) (+ acc i))))
(define (bench-main) (display (loop 20000000 0)) (newline))
