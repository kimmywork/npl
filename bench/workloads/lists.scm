; Allocation-heavy list work: build, map, reverse, fold.
(define (build n acc) (if (= n 0) acc (build (- n 1) (cons n acc))))
(define (sum xs acc) (if (null? xs) acc (sum (cdr xs) (+ acc (car xs)))))
(define (bench-main)
  (let loop ((i 0) (total 0))
    (if (= i 20)
        (begin (display total) (newline))
        (loop (+ i 1) (+ total (sum (reverse (map (lambda (x) (* 2 x)) (build 100000 '()))) 0))))))
