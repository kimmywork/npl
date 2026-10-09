; Records (R7RS 5.5) and error objects (6.11). The open-input-file case of
; file-error? is checked outside this shared fixture: Guile 3.0 answers #f
; there, contrary to R7RS 6.11 (Racket and NPL classify it as a file error).
(define (report name value) (write (list name value)) (newline))

(define-record-type <point> (make-point x y) point?
  (x point-x set-point-x!)
  (y point-y))
(define-record-type node (make-node value) node?
  (value node-value)
  (next node-next set-node-next!))
(define-record-type empty (make-empty) empty?)

(define p (make-point 1 2))
(define n (make-node 'a))
(define r1 (report 'predicates
  (list (point? p) (point? n) (node? n) (empty? (make-empty))
        (point? 1) (vector? p) (pair? p) (procedure? p))))
(define r2 (report 'access (list (point-x p) (point-y p) (node-value n))))
(define r3 (begin (set-point-x! p 10) (set-node-next! n (make-node 'b))
                  (report 'modify
                    (list (point-x p) (node-value (node-next n))))))
(define r4 (report 'unset-field-is-not-an-error
  (node? (make-node 'c))))
(define r5 (report 'distinct-instances
  (list (eq? (make-point 1 2) (make-point 1 2))
        (equal? (point-x (make-point 1 2)) 1))))

(define (catch thunk)
  (call-with-current-continuation
    (lambda (k)
      (with-exception-handler
        (lambda (e)
          (k (if (error-object? e)
                 (list 'error (error-object-message e) (error-object-irritants e))
                 (list 'raised e))))
        thunk))))
(define r6 (report 'error (catch (lambda () (error "bad thing" 1 'two "three")))))
; Guile 3.0 returns #f instead of () for absent irritants; compare the message.
(define r7 (report 'no-irritants
  (guard (e ((error-object? e) (error-object-message e))) (error "plain"))))
(define r8 (report 'raise-non-error (catch (lambda () (raise 'sym)))))
(define r9 (report 'guard
  (guard (e ((symbol? e) (list 'symbol e))
            ((error-object? e) (list 'message (error-object-message e))))
    (error "from guard" 42))))
(define r10 (report 'kinds
  (guard (e (#t (list (error-object? e) (file-error? e) (read-error? e))))
    (error "plain"))))
(define r11 (report 'read-error
  (guard (e ((read-error? e) (list 'read-error (error-object? e)
                                   (file-error? e))))
    (read (open-input-string "(1 2")))))
(define r12 (report 'non-errors
  (list (error-object? 'x) (error-object? p) (file-error? "s")
        (read-error? 3))))
(define r13 (report 'error-in-handler-continues
  (with-exception-handler
    (lambda (e) 0)
    (lambda () (+ 1 (raise-continuable (make-point 0 0)))))))
