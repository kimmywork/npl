; NPL record extensions (docs/design-data.md section 2), checked by hand:
; Guile 3.0 has no record-update and prints a cycle through a record field
; without its label (#<node v: #0#>).
;
; record-update copies an instance with named fields replaced; equal? and
; write handle cycles through record fields with the existing union-find
; pass and datum labels; wrong types and unknown fields raise error objects
; whose message names the operation.
(define (report name value) (write (list name value)) (newline))
(define (failure thunk)
  (guard (e ((error-object? e)
             (cons (error-object-message e) (error-object-irritants e))))
    (thunk)))

(define-record-type point (make-point x y) point?
  (x point-x set-point-x!)
  (y point-y))
(define-record-type node (make-node value) node?
  (value node-value)
  (next node-next set-node-next!))

(define p (make-point 1 2))
(define q (record-update point p (y 20)))
(define r (record-update point p (x 'a) (y 'b)))
(report 'update (list p q r (eq? p (record-update point p))))
(report 'update-is-a-copy
  (begin (set-point-x! q 5) (list (point-x p) (point-x q))))

(define a (make-node 'a))
(set-node-next! a a)
(define b (make-node 'a))
(set-node-next! b b)
(define c (make-node 'a))
(set-node-next! c (make-node 'a))
(write a) (newline)
(write (list a a)) (newline)
(report 'cyclic-equal (list (equal? a b) (equal? a c)))

(report 'accessor-type (failure (lambda () (point-x (make-node 1)))))
(report 'modifier-type (failure (lambda () (set-point-x! 'p 1))))
(report 'update-type (failure (lambda () (record-update point a (x 1)))))
(report 'update-field (failure (lambda () (record-update point p (z 1)))))
(report 'error-object (failure (lambda () (error "boom" p))))
