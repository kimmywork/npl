; Escape continuations in a loop.
(define (find-first pred xs)
  (call-with-current-continuation
    (lambda (k) (for-each (lambda (x) (if (pred x) (k x))) xs) #f)))
(define (bench-main)
  (let ((xs (let build ((i 100) (acc '())) (if (= i 0) acc (build (- i 1) (cons i acc))))))
    (let loop ((i 0) (acc 0))
      (if (= i 100000)
          (begin (display acc) (newline))
          (loop (+ i 1) (+ acc (find-first (lambda (x) (> x 50)) xs)))))))
