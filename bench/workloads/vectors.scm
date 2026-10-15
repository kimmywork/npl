; Vector writes and reads in nested loops.
(define (bench-main)
  (let ((v (make-vector 1000 0)))
    (let outer ((k 0))
      (if (< k 2000)
          (begin
            (let inner ((i 0))
              (if (< i 1000) (begin (vector-set! v i (+ (vector-ref v i) i)) (inner (+ i 1)))))
            (outer (+ k 1)))))
    (let sum ((i 0) (acc 0))
      (if (= i 1000) (begin (display acc) (newline)) (sum (+ i 1) (+ acc (vector-ref v i)))))))
