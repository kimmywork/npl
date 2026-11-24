; Tail calls from a top-level procedure to itself, which nplc compiles to a
; jump (O2), next to the cases that keep the trampoline.
(define (show x) (write x) (newline))
; Deep tail loop: a jump must not grow the C stack.
(define (count-down n acc) (if (= n 0) acc (count-down (- n 1) (+ acc 1))))
; Arguments read the old parameter values (parallel assignment).
(define (swap-loop n a b) (if (= n 0) (list a b) (swap-loop (- n 1) b a)))
(define (rotate n a b c) (if (= n 0) (list a b c) (rotate (- n 1) b c a)))
; Left-to-right argument evaluation with side effects.
(define trace '())
(define (note x) (set! trace (cons x trace)) x)
(define (order n) (if (= n 0) (reverse trace) (order (note (- n 1)))))
; A closure made in the body captures that iteration's parameter values.
(define (collect n acc)
  (if (= n 0)
      (map (lambda (f) (f)) acc)
      (collect (- n 1) (cons (lambda () n) acc))))
(define (collect2 n acc)
  (if (= n 0) acc (collect2 (- n 1) (cons (lambda () (* n 10)) acc))))
; A parameter that is assigned keeps the trampoline (fresh cell per call).
(define (assigned n acc)
  (if (= n 0) (map (lambda (f) (f)) acc)
      (begin (set! n (- n 1)) (assigned n (cons (lambda () n) acc)))))
; The name shadowed by a local binding is not a self call.
(define (shadow n)
  (let ((shadow (lambda (m) (list 'local m))))
    (if (= n 0) 'done (shadow (- n 1)))))
; A self call from a lambda in the body goes through the trampoline.
(define (via-lambda n) (if (= n 0) 'ok ((lambda () (via-lambda (- n 1))))))
; Non-tail self call (CPS continuation) and a value-position if per iteration.
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(define (sum-if n acc)
  (if (= n 0) acc (sum-if (- n 1) (+ acc (if (even? n) n 0)))))
; Wrong argument count to itself stays a runtime arity error.
(define (bad n) (if (= n 0) (bad 1 2) 'never))
; Mutual recursion keeps the trampoline.
(define (ev? n) (if (= n 0) #t (od? (- n 1))))
(define (od? n) (if (= n 0) #f (ev? (- n 1))))
; Non-local exit and re-entry through call/cc inside a self loop.
(define (find-first pred xs)
  (call-with-current-continuation
    (lambda (k)
      (define (walk ys) (if (null? ys) #f (begin (if (pred (car ys)) (k (car ys))) (walk (cdr ys)))))
      (walk xs))))
(define (escape-loop n k) (if (= n 3) (k 'escaped) (escape-loop (+ n 1) k)))
(define saved #f)
(define (resume-loop n acc)
  (if (= n 0) acc
      (resume-loop (- n 1)
        (+ acc (call-with-current-continuation (lambda (c) (if (not saved) (set! saved c)) 1))))))
; Errors inside a self loop are catchable.
(define (err-loop n) (if (= n 0) (car '()) (err-loop (- n 1))))
; A body with a string allocation per iteration and a guard.
(define (str-loop n acc) (if (= n 0) (string-length acc) (str-loop (- n 1) (string-append acc "x"))))

(show (count-down 3000000 0))
(show (list (swap-loop 5 'a 'b) (swap-loop 6 'a 'b) (rotate 4 1 2 3)))
(show (order 4))
(show (collect 4 '()))
(show (map (lambda (f) (f)) (collect2 3 '())))
(show (assigned 3 '()))
(show (shadow 5))
(show (via-lambda 100000))
(show (list (fib 20) (sum-if 100 0)))
(show (guard (e ((error-object? e) 'arity-error)) (bad 0)))
(show (list (ev? 100001) (od? 7)))
(show (list (find-first even? '(1 3 4 5 6)) (call-with-current-continuation (lambda (k) (escape-loop 0 k)))))
(show (resume-loop 5 0))
(show (guard (e (#t 'caught)) (err-loop 100)))
(show (str-loop 1000 ""))
