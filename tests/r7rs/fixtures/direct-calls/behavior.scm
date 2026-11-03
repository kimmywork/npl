;; Direct entries (docs/design-calls.md C2b): direct-safe functions called
;; in value position, including the stack-limit fallback, errors, bignums,
;; continuations, and closures made inside them.
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(define (tak x y z)
  (if (not (< y x))
      z
      (tak (tak (- x 1) y z) (tak (- y 1) z x) (tak (- z 1) x y))))
(define (build n acc) (if (= n 0) acc (build (- n 1) (cons n acc))))
(define (len xs) (if (null? xs) 0 (+ 1 (len (cdr xs)))))
(define (sum-list xs) (if (null? xs) 0 (+ (car xs) (sum-list (cdr xs)))))
(define (count-up n) (if (= n 0) '() (cons n (count-up (- n 1)))))
(define (fact n) (if (= n 0) 1 (* n (fact (- n 1)))))
(define (add2 a b) (+ a b))
(define (twice-sub a b) (- (add2 a a) b))
(define (adder n) (lambda (x) (+ x n)))
(define (counter start)
  (let ((count start))
    (lambda () (set! count (+ count 1)) count)))
(define (my-even? n) (if (= n 0) #t (my-odd? (- n 1))))
(define (my-odd? n) (if (= n 0) #f (my-even? (- n 1))))
(define (first-of xs) (car xs))

;; Deep non-tail recursion: past the stack limit the call continues in CPS.
(display (list (fib 20) (tak 18 12 6))) (newline)
(display (len (build 1000000 '()))) (newline)
(display (sum-list (build 1000000 '()))) (newline)
(display (len (count-up 1000000))) (newline)
;; A primitive error inside a direct call reaches the caller's guard.
(display (guard (e (#t 'caught)) (len 5))) (newline)
(display (guard (e (#t 'caught)) (first-of '()))) (newline)
(display (guard (e (#t (list 'outer 'caught))) (+ 1 (len (cons 1 2)))))
(newline)
;; The same error 1,000,000 calls deep, raised in the CPS fallback.
(display (guard (e (#t 'deep-caught)) (len (build 1000000 5)))) (newline)
;; Bignum and flonum operands take the library slow paths.
(display (fact 25)) (newline)
(display (add2 4611686018427387903 1)) (newline)
(display (twice-sub -2305843009213693952 1)) (newline)
(display (add2 1.5 2)) (newline)
(display (fib 10.0)) (newline)
;; call/cc escaping around, and while evaluating operands of, direct calls.
(display (call/cc (lambda (k) (+ 1 (fib 15) (k 'escaped))))) (newline)
(display (+ 1 (call/cc (lambda (k) (fib (k 10)))))) (newline)
(display
  (let ((resume #f) (round 0) (seen '()))
    (let ((value (+ (fib 10) (call/cc (lambda (k) (set! resume k) 0)))))
      (set! seen (cons value seen))
      (set! round (+ round 1))
      (if (< round 3) (resume round) (reverse seen)))))
(newline)
;; Closures made in a direct function and called later.
(define add3 (adder 3))
(define next (counter 10))
(define first-tick (next))
(display (list (add3 4) (map (adder 1) '(1 2 3)) (next) (next))) (newline)
;; Mutual tail recursion keeps constant stack (no direct entry).
(display (list (my-even? 1000001) (my-odd? 1000001))) (newline)
