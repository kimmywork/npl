; Safe primitives raise catchable R7RS error objects (docs/plan-r7rs.md
; "Primitive Errors"). Only catchability and error-object? are shared with
; Guile; message and irritant text is compared between native and npli by
; src/compiler/tests/primitive-errors.npl.
(define (report name value) (write (list name value)) (newline))
(define (kind thunk)
  (guard (e ((error-object? e) 'error-object) (#t 'other))
    (thunk)
    'returned))
(define (apply-to f x) (f x))
(define r1 (report 'car (kind (lambda () (car 1)))))
(define r2 (report 'cdr (kind (lambda () (cdr '())))))
(define r3 (report 'vector-ref-range (kind (lambda () (vector-ref (vector 1 2) 5)))))
(define r4 (report 'vector-ref-type (kind (lambda () (vector-ref 'v 0)))))
(define r5 (report 'vector-set-range (kind (lambda () (vector-set! (make-vector 2 0) 2 0)))))
(define r6 (report 'char->integer (kind (lambda () (char->integer 1)))))
(define r7 (report 'string-ref (kind (lambda () (string-ref "ab" 9)))))
(define r8 (report 'arity (kind (lambda () (apply-to (lambda (x y) x) 1)))))
(define r9 (report 'not-a-procedure (kind (lambda () (apply-to 1 2)))))
(define r10 (report 'nested
  (guard (outer (#t (list 'outer (error-object? outer))))
    (guard (inner ((string? inner) 'wrong-clause))
      (car 1)))))
; The log is read after guard returns. NPL's guard currently evaluates its
; clauses before unwinding (R7RS 4.2.7 says in guard's dynamic environment);
; tracked in .agents/workstate.md.
(define r11 (report 'unwinds-dynamic-wind
  (let ((log '()))
    (guard (e (#t 'handled))
      (dynamic-wind
        (lambda () (set! log (cons 'before log)))
        (lambda () (vector-ref (vector) 0))
        (lambda () (set! log (cons 'after log)))))
    (reverse log))))
(define r12 (report 'handler-escape
  (call-with-current-continuation
    (lambda (k)
      (with-exception-handler
        (lambda (e) (k (error-object? e)))
        (lambda () (+ 1 (cdr 5))))))))
(define r13 (report 'repeated
  (let loop ((i 0) (caught 0))
    (if (= i 500)
        caught
        (loop (+ i 1)
              (+ caught (if (eq? (kind (lambda () (car i))) 'error-object) 1 0)))))))
(define r14 (report 'after-errors (car (list 'ok))))
