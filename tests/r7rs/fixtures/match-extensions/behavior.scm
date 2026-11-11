; NPL match extensions (.agents/archive/design/design-data.md section 4), checked by hand:
; Guile 3.0's (ice-9 match) has no match-define and raises a match-error
; throw instead of an error object.
;
; match-define binds a pattern's variables as definitions; a failed match
; raises an error object whose irritant is the value; ($ type p ...) with
; more patterns than the type has fields raises one too; a set! pattern
; inside $ assigns the record field.
(define (report name value) (write (list name value)) (newline))
(define (failure thunk)
  (guard (e ((error-object? e)
             (cons (error-object-message e) (error-object-irritants e))))
    (thunk)))

(define-record-type point (make-point x y) point?
  (x point-x)
  (y point-y))
(define-record-type other (make-other a) other? (a other-a))

(match-define (top-a (top-b . top-rest)) '(1 (2 3 4)))
(report 'match-define (list top-a top-b top-rest))
(report 'match-define-body
  (let ()
    (match-define #(x (y ...)) (vector 'v '(1 2)))
    (list x y)))
(report 'match-define-failure
  (failure (lambda () (let () (match-define (a b) '(1)) a))))
(report 'no-match (failure (lambda () (match '(1 2) ((a) a) (5 5)))))
(report 'no-clause-matches-record
  (failure (lambda () (match (make-other 1) (($ point a b) a)))))
(report 'too-many-field-patterns
  (failure (lambda () (match (make-point 1 2) (($ point a b c) c)))))
(report 'record-set
  (let ((p (make-point 1 2)))
    (match p (($ point (set! set-x) _) (set-x 10)))
    (list (point-x p) (point-y p))))
(report 'record-type-mismatch
  (match (make-other 1) (($ point a b) 'point) (($ other a) (list 'other a))))
