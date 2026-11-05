;; Direct entries of lambdas and closure call sites (docs/design-calls.md
;; C3): each case runs on the fast path when the callee has a direct entry
;; and on the CPS path otherwise; both must give the same observable result.
(define (my-map f items out)
  (if (null? items) (reverse out) (my-map f (cdr items) (cons (f (car items)) out))))
(define (iota-list n acc) (if (= n 0) acc (iota-list (- n 1) (cons n acc))))
(define big (iota-list 100000 '()))

;; Direct lambdas through map, for-each, and a user map.
(display (list (map (lambda (x) (* x x)) '(1 2 3))
               (my-map (lambda (x) (+ x 1)) '(1 2 3) '())
               (let ((k 10)) (map (lambda (x) (+ x k)) '(1 2)))
               (apply + (map (lambda (x) (* 2 x)) big))))
(newline)
(define total 0)
(for-each (lambda (x) (set! total (+ total x))) big)
(display total) (newline)

;; A lambda that assigns a captured variable (shared cell).
(define (counter)
  (let ((n 0)) (lambda (x) (set! n (+ n x)) n)))
(define c (counter))
(display (map c '(1 2 3 4))) (newline)

;; Errors raised in a direct lambda reach the caller's guard; bignums.
(display (list (guard (e ((error-object? e) (quote error-object)))
                 (map (lambda (x) (car x)) '((1) 2)))
               (guard (e ((symbol? e) e))
                 (my-map (lambda (x) (if (> x 2) (raise 'too-big) x)) '(1 2 3) '()))
               (map (lambda (x) (* x 4611686018427387904)) '(1 -1))))
(newline)

;; Slow path: a procedure that captures a continuation, a non-procedure,
;; wrong arity, a primitive, and a variadic lambda.
(display (list (call/cc (lambda (k) (map (lambda (x) (if (= x 2) (k 'escaped) x)) '(1 2 3))))
               (guard (e ((error-object? e) 'not-a-procedure)) (my-map 5 '(1) '()))
               (guard (e ((error-object? e) 'arity)) (my-map (lambda (x y) x) '(1) '()))
               (map car '((1) (2)))
               (map (lambda args (length args)) '(1 2))))
(newline)

;; A direct lambda that recurses deeply through a top-level function.
(define (depth n) (if (= n 0) 0 (+ 1 (depth (- n 1)))))
(display (map (lambda (n) (depth n)) '(10 1000000))) (newline)
