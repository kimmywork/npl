; Datum labels in read (R7RS 2.4). Guile 3.0's read lacks them (#1= is
; array syntax there), so this fixture has no Guile run (oracle=none in
; fixture.meta); every expected line follows R7RS 2.4 and was checked
; against Racket 9.2's read (eq? sharing, cycles, and errors agree).
(define (report name value) (write (list name value)) (newline))
(define (rd s) (read (open-input-string s)))
(define (second x) (car (cdr x)))
(define (third x) (car (cdr (cdr x))))

(define r1 (report 'cyclic-list
  (let ((x (rd "#0=(a b . #0#)")))
    (list (car x) (second x) (eq? x (cdr (cdr x)))))))
(define r2 (report 'shared-elements
  (let ((y (rd "(#1=(x) #1# #1#)")))
    (list (eq? (car y) (second y)) (eq? (car y) (third y)) (car y)))))
(define r3 (report 'cyclic-vector
  (let ((v (rd "#2=#(1 #2# 3)")))
    (list (vector-ref v 0) (eq? v (vector-ref v 1)) (vector-ref v 2)))))
(define r4 (report 'nested-labels
  (let ((z (rd "#10=(#11=(p) . #11#)")))
    (list (eq? (car z) (cdr z)) (car z)))))
(define r5 (report 'atom-labels (rd "(#0=1 #0# #1=\"s\" #1#)")))
(define r6 (report 'label-inside-vector
  (let ((w (rd "#(#3=(q) #3#)")))
    (eq? (vector-ref w 0) (vector-ref w 1)))))
(define r7 (report 'errors
  (map (lambda (s) (guard (e ((read-error? e) 'read-error)) (rd s)))
       (list "#5#" "#3=#3#" "(#4=1 #4=2)"))))
(define r8 (report 'labels-are-per-read
  (let ((p (open-input-string "#0=(a) #0#")))
    (let ((first (read p)))
      (list first (guard (e ((read-error? e) 'read-error)) (read p)))))))
