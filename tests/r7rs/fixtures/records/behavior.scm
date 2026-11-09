; define-record-type (R7RS 5.5, docs/design-data.md section 2): access,
; mutation, predicates, constructor argument order, generativity, equal?
; field by field, and the printed form (Guile's #<name field: value>).
(define (report name value) (write (list name value)) (newline))

(define-record-type <point> (make-point x y) point?
  (x point-x set-point-x!)
  (y point-y set-point-y!))
(define-record-type pair-box (make-pair-box right left) pair-box?
  (left pair-box-left)
  (right pair-box-right))
(define-record-type tagged (make-tagged) tagged?
  (tag tagged-tag set-tagged-tag!))

(define p (make-point 1 2))
(report 'access (list (point-x p) (point-y p)))
(set-point-x! p 10)
(set-point-y! p (list 'a "b"))
(report 'mutate (list (point-x p) (point-y p)))
(report 'argument-order
  (let ((b (make-pair-box 'r 'l))) (list (pair-box-left b) (pair-box-right b))))
(report 'unset-field
  (let ((t (make-tagged)))
    (set-tagged-tag! t 'set)
    (tagged-tag t)))
(report 'predicates
  (list (point? p) (point? (make-pair-box 1 2)) (pair-box? p) (point? 'p)
        (point? (vector 10 2)) (vector? p) (procedure? p)))
(report 'first-class (map point-x (list (make-point 1 2) (make-point 3 4))))

; Each evaluation of a definition makes a new type.
(define (make-type-instance value)
  (define-record-type cell (make-cell value) cell? (value cell-value))
  (cons cell? (make-cell value)))
(define first-type (make-type-instance 1))
(define second-type (make-type-instance 2))
(report 'generative
  (list ((car first-type) (cdr first-type))
        ((car first-type) (cdr second-type))))

(report 'equal
  (list (equal? (make-point 1 2) (make-point 1 2))
        (equal? (make-point 1 2) (make-point 1 3))
        (equal? (make-point 1 (list 2 "s")) (make-point 1 (list 2 "s")))
        (equal? (make-point (make-point 1 2) 3) (make-point (make-point 1 2) 3))
        (equal? (make-point 1 2) (make-pair-box 2 1))
        (eqv? (make-point 1 2) (make-point 1 2))
        (equal? (list (make-point 1 2)) (list (make-point 1 2)))))

(write (make-point 1 "two")) (newline)
(display (make-point 1 "two")) (newline)
(write (list (make-pair-box 'r 'l) (make-point (make-point 0 0) #\a))) (newline)
(write <point>) (newline)
