;; Named-let and internal-define loops, which nplc compiles to jumps when the
;; loop name is assigned once (O2b), next to the cases that keep the
;; trampoline.
(define (show x) (write x) (newline))

;; Plain named let, internal define, and nested named lets.
(define (sum-to n) (let loop ((i 0) (acc 0)) (if (> i n) acc (loop (+ i 1) (+ acc i)))))
(define (count-evens xs)
  (define (walk ys k) (if (null? ys) k (walk (cdr ys) (if (even? (car ys)) (+ k 1) k))))
  (walk xs 0))
(define (grid n)
  (let outer ((i 0) (rows '()))
    (if (= i n)
        (reverse rows)
        (outer (+ i 1)
               (cons (let inner ((j 0) (row '()))
                       (if (= j n) (reverse row) (inner (+ j 1) (cons (* i j) row))))
                     rows)))))

;; Arguments read the old values (swap), left to right with side effects.
(define (swap n) (let loop ((k n) (a 'x) (b 'y)) (if (= k 0) (list a b) (loop (- k 1) b a))))
(define trace '())
(define (note x) (set! trace (cons x trace)) x)
(define (ordered n) (let loop ((i n)) (if (= i 0) (reverse trace) (loop (note (- i 1))))))

;; A closure made in the loop body sees that iteration's values.
(define (thunks n)
  (let loop ((i 0) (acc '()))
    (if (= i n) (map (lambda (f) (f)) acc) (loop (+ i 1) (cons (lambda () i) acc)))))

;; Deep loop: a jump must not grow the C stack.
(define (spin n) (let loop ((i n)) (if (= i 0) 'done (loop (- i 1)))))

;; The loop name assigned again: calls must follow the assignment.
(define (reassigned)
  (letrec ((loop (lambda (i) (if (= i 0) 'first (loop (- i 1))))))
    (set! loop (lambda (i) (list 'second i)))
    (loop 3)))
;; The loop passed out and called later, and called from a nested lambda.
(define (escaped)
  (let ((f (let loop ((i 2)) (if (= i 0) loop (loop (- i 1))))))
    (eq? (f 0) f)))
(define (via-lambda n) (let loop ((i n)) (if (= i 0) 'ok ((lambda () (loop (- i 1)))))))
;; A loop name shadowed inside the body is not the loop.
(define (shadowed)
  (let loop ((i 3))
    (let ((loop (lambda (j) (list 'inner j))))
      (if (= i 0) 'never (loop i)))))
;; Wrong arity to the loop is still the arity error.
(define (bad-arity) (let loop ((i 1)) (if (= i 0) 'never (loop 1 2))))
;; Mutually recursive internal definitions keep the trampoline.
(define (parity n)
  (define (ev? k) (if (= k 0) #t (od? (- k 1))))
  (define (od? k) (if (= k 0) #f (ev? (- k 1))))
  (list (ev? n) (od? n)))
;; A loop inside a self-looping top-level function: each outer iteration
;; makes a fresh loop.
(define (outer-self k acc)
  (if (= k 0) (reverse acc)
      (outer-self (- k 1) (cons (let loop ((i k) (p 1)) (if (= i 0) p (loop (- i 1) (* p 2)))) acc))))
;; call/cc escape and re-entry inside a loop; guard around a failing loop.
(define (first-big xs)
  (call-with-current-continuation
    (lambda (k) (let loop ((ys xs)) (if (null? ys) #f (begin (if (> (car ys) 10) (k (car ys))) (loop (cdr ys))))))))
(define saved #f)
(define (resume n)
  (let loop ((i n) (acc 0))
    (if (= i 0) acc
        (loop (- i 1) (+ acc (call-with-current-continuation (lambda (c) (if (not saved) (set! saved c)) 1)))))))
(define (fails n) (let loop ((i n)) (if (= i 0) (car '()) (loop (- i 1)))))
;; Collections while a loop keeps captured values live.
(define (alloc-loop n)
  (let ((s (make-string 4 #\q)))
    (let loop ((i 0) (acc '()))
      (if (= i n) (length acc) (loop (+ i 1) (cons (string-append s (make-string 8 #\z)) acc))))))

(show (list (sum-to 100) (count-evens '(1 2 3 4 6)) (grid 3)))
(show (list (swap 3) (swap 4) (ordered 4)))
(show (thunks 4))
(show (spin 3000000))
(show (list (reassigned) (escaped) (via-lambda 100000) (shadowed)))
(show (guard (e ((error-object? e) 'arity-error)) (bad-arity)))
(show (list (parity 10001) (outer-self 4 '())))
(show (list (first-big '(1 5 20 30)) (resume 5)))
(show (guard (e (#t 'caught)) (fails 100)))
(show (alloc-loop 50000))
