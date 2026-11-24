(define (report name value)
  (write (list name value))
  (newline))

(define (binding-results)
  (report 'parallel
    (let ((x 7)) (let ((x 1) (y x)) (list x y))))
  (report 'sequential-shadow
    (let* ((x 1) (f (lambda () x)) (x 2)) (list (f) x)))
  (report 'recursive
    (letrec ((even (lambda (n) (if (= n 0) #t (odd (- n 1)))))
             (odd (lambda (n) (if (= n 0) #f (even (- n 1))))))
      (list (even 8) (odd 9))))
  (report 'recursive-sequential
    (letrec* ((x 1) (y (+ x 1))) y))
  (report 'forward-closure
    (letrec* ((f (lambda () x)) (x 42)) (f)))
  (report 'internal-scope
    (let ((x 10))
      ((lambda ()
         (define (f) x)
         (define x 42)
         (f)))))
  (report 'empty-sequential-body
    (let* () (define x 40) (define y (+ x 2)) y))
  (report 'empty-recursive-body
    (letrec* () (define x 40) (define y (+ x 2)) y))
  (report 'binding-values
    (call-with-values
      (lambda () (let* ((x 20) (y (+ x 2))) (values x y)))
      (lambda results results)))
  (report 'continuation-zero
    (call-with-values
      (lambda () (call/cc (lambda (k) (k))))
      (lambda results results)))
  (report 'continuation-many
    (call-with-values
      (lambda () (call/cc (lambda (k) (k 20 22))))
      (lambda results results)))
  (report 'initializer-error
    (let ((trace '()))
      (let ((result
              (guard (object (else 'caught))
                (let* ((x (begin (set! trace (cons 1 trace)) 1))
                       (y (fixture-raise 'initializer))
                       (z (begin (set! trace (cons 3 trace)) 3)))
                  z))))
        (list result trace)))))

(define saved-binding #f)
(define binding-phase 0)
(define (resumed-binding)
  (let* ((x (call/cc (lambda (k) (set! saved-binding k) 10)))
         (y (+ x 1)))
    (if (= binding-phase 0)
        (begin (set! binding-phase 1) (saved-binding 20))
        (list x y))))

(define (scalar-results)
  (report 'vector-tag-isolation
    (let ((value (vector 903 42)))
      (list (vector? value) (vector-ref value 0) (vector-ref value 1)
            (vector? (vector 901)) (vector? (vector 914))
            (vector? (vector '(903) 42)))))
  (report 'comparison-width
    (list (= 4294967296 0) (< 2147483648 0)
          (> 4294967296 0) (<= 2147483648 2147483648)
          (>= (- 0 2147483649) (- 0 2147483648))
          (< (- (- 0 2305843009213693951) 1) 2305843009213693951)
          (= 1 1 1)
          (< 1 2 3)
          (> 3 2 1)))
  (report 'comparison-chain
    (list (<= 1 1 2) (>= 3 3 1) (= 1 1 2)
          (< 1 3 2) (> 3 1 2) (<= 1 2 1) (>= 3 2 3)))
  (report 'comparison-effects
    (let ((calls 0))
      (let ((result (< 3 2 (begin (set! calls (+ calls 1)) 1))))
        (list result calls))))
  (report 'pair-tags
    (list (pair? '(1)) (pair? (cons 1 2)) (pair? '())
          (pair? '#(1)) (pair? "abcdef") (pair? 'abcdef)
          (pair? (lambda () 1)))))

(define fixture-result
  (begin
    (binding-results)
    (report 'resumed-binding (resumed-binding))
    (scalar-results)
    0))
