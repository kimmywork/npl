; Calls to (scheme base) numeric operators: the fixnum fast path, the library
; slow path (non-fixnum operands, overflow, errors), and non-local exits from
; the slow path in value and tail position.
(define (show x) (write x) (newline))
(define (add x y) (+ x y))
(define (less x y) (< x y))
(define (chain x y z) (<= x y z))
(define (negate x) (- x))
(define (count-down n) (if (< n 1) 'done (count-down (- n 1))))
(define (sum-to n acc) (if (= n 0) acc (sum-to (- n 1) (+ acc n))))
(define (caught thunk)
  (guard (e ((error-object? e) 'error) (#t (list 'raised e))) (thunk)))

(define r1 (show (list (+) (*) (+ 1) (- 5) (* 3 4 5) (+ 1 2 3 4))))
(define r2 (show (list (+ 1 2.5) (- 1/2 1) (* 2 0.25) (- 10 2 3))))
(define r3
  (show (list (+ 4611686018427387903 1) (- -4611686018427387904 1)
              (* 4611686018427387903 2)
              (add 4611686018427387903 4611686018427387903))))
(define r4
  (show (list (< 1 2 3) (< 1 3 2) (= 2 2 2.0) (>= 3 3 1) (> 2 1.5) (<= 1 1))))
(define r5 (show (list (add 1 2) (add 1.5 2) (less 1 2) (less 2.0 1))))
(define r6
  (show (list (chain 1 2 3) (chain 1 3 2) (chain 1 2 2.5)
              (negate 3) (negate 2.5))))
(define r7 (show (list (count-down 10) (sum-to 100000 0))))
(define r8
  (show (list (map + '(1 2) '(10 20)) (apply + 1 2 '(3 4))
              (let ((p <)) (p 1 2)))))
(define r9
  (show (list (caught (lambda () (+ 1 'a))) (caught (lambda () (< 1 "x")))
              (caught (lambda () (add 1 'b))) (caught (lambda () (less #t 1)))
              (caught (lambda () (-))))))
(define r10
  (show (guard (e (#t 'outer)) (guard (e ((string? e) 'inner)) (+ 1 'a)))))
; Only the final log is observed: when guard clauses run relative to the
; after thunk is a separate open item.
(define r11
  (show (let ((log '()))
          (guard (e (#t 'handled))
            (dynamic-wind
              (lambda () (set! log (cons 'in log)))
              (lambda () (- 1 'z))
              (lambda () (set! log (cons 'out log)))))
          (reverse log))))
(define r12
  (show (with-exception-handler
          (lambda (e) 0)
          (lambda () (+ 1 (raise-continuable 'c) 2.5)))))
(define r13
  (show (let loop ((i 0) (acc 0))
          (if (= i 1000)
              acc
              (loop (+ i 1) (+ acc (guard (e (#t 1)) (+ i 'x))))))))
(define r14
  (show (call-with-current-continuation (lambda (k) (+ 1 (k 'escaped) 2.5)))))
(define r15
  (show (map (lambda (x) (guard (e (#t 'bad)) (+ x 0.5))) '(1 a 2))))
(define r16 (show (+ 1 (guard (e (#t 10)) (< 1 'a)) 2.5)))
