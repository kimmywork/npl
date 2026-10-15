; Closure creation and higher-order calls.
; 50000 iterations: fast enough for every implementation, slow enough to be
; workload-bound rather than noise.
(define (compose f g) (lambda (x) (f (g x))))
(define (bench-main)
  (let loop ((i 0) (acc 0))
    (if (= i 50000)
        (begin (display acc) (newline))
        (loop (+ i 1) ((compose (lambda (x) (+ x 1)) (lambda (x) (* x 2))) acc)))))
