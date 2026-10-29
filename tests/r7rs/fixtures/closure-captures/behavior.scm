;; Closures store their captured values in the closure object (flat
;; closures, design-calls C1b). Captures of 0, 1, and 20 values, shared
;; assigned cells, re-captured captures, and captures read after many
;; collections.
(define (show x) (write x) (newline))

(define (no-capture) (lambda (x) (* x 2)))
(define (one-capture n) (lambda (x) (+ x n)))
(define (twenty)
  (let ((c0 (* 0 10)) (c1 (* 1 10)) (c2 (* 2 10)) (c3 (* 3 10)) (c4 (* 4 10)) (c5 (* 5 10)) (c6 (* 6 10)) (c7 (* 7 10)) (c8 (* 8 10)) (c9 (* 9 10)) (c10 (* 10 10)) (c11 (* 11 10)) (c12 (* 12 10)) (c13 (* 13 10)) (c14 (* 14 10)) (c15 (* 15 10)) (c16 (* 16 10)) (c17 (* 17 10)) (c18 (* 18 10)) (c19 (* 19 10)))
    (lambda () (list c0 c1 c2 c3 c4 c5 c6 c7 c8 c9 c10 c11 c12 c13 c14 c15 c16 c17 c18 c19))))

;; A set! target is captured as a shared cell: both closures see updates.
(define (counter)
  (let ((n 0))
    (cons (lambda () (set! n (+ n 1)) n)
          (lambda () n))))

;; An inner lambda re-captures values the outer lambda captured.
(define (nest a b)
  (lambda (c)
    (lambda (d) (list a b c d))))

;; Closures made in a loop each keep their own captured value.
(define (make-list-of-adders n acc)
  (if (= n 0) acc (make-list-of-adders (- n 1) (cons (lambda (x) (+ x n)) acc))))

;; Captures stay valid across collections: build many closures, allocate,
;; then call them.
(define (churn n acc)
  (if (= n 0) acc (churn (- n 1) (cons (make-vector 8 n) acc))))
(define (survivors n)
  (let loop ((i 0) (fs '()))
    (if (= i n)
        (begin (churn 20000 '())
               (let sum ((fs fs) (acc 0))
                 (if (null? fs) acc (sum (cdr fs) (+ acc ((car fs)))))))
        (loop (+ i 1)
              (cons (let ((s (make-string 3 #\z)) (k i))
                      (lambda () (+ k (string-length s))))
                    fs)))))

(show ((no-capture) 21))
(show ((one-capture 40) 2))
(show ((twenty)))
(show (let ((c (counter))) ((car c)) ((car c)) (list ((car c)) ((cdr c)))))
(show (((nest 1 2) 3) 4))
(show (map (lambda (f) (f 100)) (make-list-of-adders 5 '())))
(show (survivors 2000))
(show (let ((procs (map (lambda (k) (lambda () (* k k))) '(1 2 3 4))))
        (map (lambda (p) (p)) procs)))
(show (apply (lambda args (length args)) (map (lambda (i) i) '(1 2 3))))
