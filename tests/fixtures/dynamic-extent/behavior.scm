; Dynamic extent of guard clauses and exit. R7RS 4.2.7: guard clauses run
; in the dynamic environment of the guard, after the body's after thunks;
; with no matching clause the condition is re-raised continuably. R7RS
; 6.14: exit runs every outstanding after thunk, innermost first.
(define (show x) (write x) (newline))
(define trace '())
(define (note event) (set! trace (cons event trace)))
(define (traced label thunk)
  (dynamic-wind (lambda () (note (list 'before label)))
                thunk
                (lambda () (note (list 'after label)))))
(define r1
  (begin
    (set! trace '())
    (show (list 'clause-order
                (guard (e (#t (note 'clause) (reverse trace)))
                  (traced 'a (lambda () (raise 'boom))))))))
(define r2
  (show (list 'continuable
              (with-exception-handler
                (lambda (c) 42)
                (lambda ()
                  (guard (e ((eq? e 'other) 'no))
                    (+ 1 (raise-continuable 'boom))))))))
(define r3
  (show (list 'values
              (call-with-values
                (lambda () (guard (e (#t 0)) (values 1 2)))
                list))))
(define r4
  (begin
    (set! trace '())
    (let* ((message (guard (e ((error-object? e) (error-object-message e)))
                      (traced 'c (lambda () (error "failed" 1)))))
           (events (reverse trace)))
      (show (list 'error-message message events)))))
(define r5
  (dynamic-wind
    (lambda () #f)
    (lambda ()
      (dynamic-wind (lambda () #f)
                    (lambda () (exit 3))
                    (lambda () (show 'inner))))
    (lambda () (show 'outer))))
