; Mutable-binding and closure-capture semantics (R7RS 4.1.4, 4.1.6, 4.2.2,
; 5.3). Same-named variables are deliberately shadowed so that a set! on one
; binding must not leak into, or be lost by, another binding of that name.
(define (report name value) (write (list name value)) (newline))

(define (make-counter)
  (let ((n 0))
    (lambda () (set! n (+ n 1)) n)))

(define (shared-cell)
  (let ((x 1))
    (let ((get (lambda () x))
          (put (lambda (v) (set! x v))))
      (put 5)
      (list x (get)))))

(define (capture-before-set)
  (let ((x 1))
    (let ((get (lambda () x)))
      (set! x 2)
      (get))))

(define (parameter-set x)
  (let ((before (lambda () x)))
    (set! x (* x 10))
    (list x (before))))

(define (shadow-outer-assigned)
  (let ((x 1))
    (let ((inner (let ((x 100)) (lambda () x))))
      (set! x 2)
      (list x (inner)))))

(define (shadow-inner-assigned)
  (let ((x 1))
    (let ((f (lambda (x) (set! x (+ x 1)) x)))
      (list (f 10) x))))

(define (same-name-sibling-functions)
  (list ((lambda (v) (set! v (+ v 1)) v) 1)
        ((lambda (v) (lambda () v)) 7)))

(define (letrec-mutual)
  (letrec ((even? (lambda (n) (if (= n 0) #t (odd? (- n 1)))))
           (odd? (lambda (n) (if (= n 0) #f (even? (- n 1))))))
    (list (even? 100) (odd? 7))))

(define (internal-define-set)
  (define total 0)
  (define (add! v) (set! total (+ total v)))
  (add! 3)
  (add! 4)
  total)

(define (let-star-rebind)
  (let* ((x 1) (f (lambda () x)) (x 2))
    (set! x 3)
    (list x (f))))

(define (call-all fs)
  (if (null? fs) '() (cons ((car fs)) (call-all (cdr fs)))))

(define (loop-accumulate)
  (letrec ((loop (lambda (i acc)
                   (if (= i 3)
                       acc
                       (loop (+ i 1) (cons (lambda () i) acc))))))
    (loop 0 '())))

(define (variadic-set . rest)
  (set! rest (cons 0 rest))
  rest)

(define (do-loop-closures)
  (do ((i 0 (+ i 1))
       (acc '() (cons (lambda () i) acc)))
      ((= i 3) (call-all acc))))

(define (set-in-continuation)
  (let ((x 0))
    (call-with-current-continuation
      (lambda (k) (set! x 1) (k 0)))
    x))

(define (independent-counters)
  (let ((a (make-counter)) (b (make-counter)))
    (a) (a) (b)
    (list (a) (b))))

(define (literal-looks-like-set)
  ; A quoted list shaped like an assignment is data, not code.
  (let ((x 1))
    (let ((data '(217 x 2)))
      (list x (length data)))))

(define (mutable-results)
  (report 'counter (let ((c (make-counter))) (c) (c) (c)))
  (report 'shared-cell (shared-cell))
  (report 'capture-before-set (capture-before-set))
  (report 'parameter-set (parameter-set 4))
  (report 'shadow-outer-assigned (shadow-outer-assigned))
  (report 'shadow-inner-assigned (shadow-inner-assigned))
  (report 'sibling-functions
          (let ((r (same-name-sibling-functions)))
            (list (car r) ((car (cdr r))))))
  (report 'letrec-mutual (letrec-mutual))
  (report 'internal-define-set (internal-define-set))
  (report 'let-star-rebind (let-star-rebind))
  (report 'loop-closures (call-all (loop-accumulate)))
  (report 'variadic-set (variadic-set 1 2))
  (report 'do-loop (do-loop-closures))
  (report 'continuation-set (set-in-continuation))
  (report 'independent-counters (independent-counters))
  (report 'literal-looks-like-set (literal-looks-like-set)))
(define fixture-result (begin (mutable-results) 0))
