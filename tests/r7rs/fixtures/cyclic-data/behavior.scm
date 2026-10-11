; Output and equality on circular and shared data (R7RS 2.4, 6.1, 6.13.3).
; Guile 3.0 cannot serve as the oracle (oracle=none): its write and display
; print cycles as the invalid #-1#, its write-shared numbers labels from 1,
; and its equal? does not terminate on circular lists. Expected lines follow
; R7RS (labels from 0 as in 2.4; write labels only cycles, write-shared all
; sharing, write-simple none) and were cross-checked against Racket 9.2's
; write with print-graph and its equal?.
(define (report name value) (write (list name value)) (newline))
(define (text procedure value)
  (let ((out (open-output-string)))
    (procedure value out)
    (get-output-string out)))
(define (cycle . items)
  (let ((head (apply list items)))
    (let loop ((p head))
      (if (null? (cdr p)) (set-cdr! p head) (loop (cdr p))))
    head))
(define shared (list 'x))
(define self-vector (let ((v (vector 1 2))) (vector-set! v 1 v) v))
(define car-cycle (let ((c (list 1 2 3))) (set-car! (cdr c) c) c))
(define tail-shared (let ((x (list 1 2))) (list x (cdr x))))

(define r1 (report 'write-cycle (text write (cycle 1 2))))
(define r2 (report 'display-cycle (text display (cycle "a" #\b))))
(define r3 (report 'write-shared-cycle (text write-shared (cycle 1 2))))
(define r4 (report 'write-no-cycle (text write (list shared (vector shared shared) shared))))
(define r5 (report 'write-shared-sharing
  (text write-shared (list shared (vector shared shared) shared))))
(define r6 (report 'vector-cycle (text write (list self-vector self-vector))))
(define r7 (report 'vector-shared (text write-shared (list self-vector self-vector))))
(define r8 (report 'car-cycle (text write car-cycle)))
(define r9 (report 'tail-shared
  (list (text write tail-shared) (text write-shared tail-shared))))
(define r10 (report 'write-simple (text write-simple (list shared shared))))
(define r11 (report 'read-back
  (let ((back (read (open-input-string (text write (cycle 'p 'q))))))
    (list (car back) (car (cdr back)) (eq? back (cdr (cdr back)))))))
(define r12 (report 'equal-cycles
  (list (equal? (cycle 'a 'b) (cycle 'a 'b 'a 'b))
        (equal? (cycle 1) (cycle 1 1 1))
        (equal? (cycle 'a 'b) (cycle 'a 'c))
        (equal? (cycle 1) (list 1 1 1))
        (equal? self-vector (let ((w (vector 1 2))) (vector-set! w 1 w) w))
        (equal? car-cycle (let ((c (list 1 2 3))) (set-car! (cdr c) c) c)))))
(define r13 (report 'equal-acyclic
  (list (equal? '(1 (2 #(3 "x")) . 4) (cons 1 (cons (list 2 (vector 3 "x")) 4)))
        (equal? (make-list 1000 'z) (make-list 1000 'z))
        (equal? (make-list 1000 'z) (make-list 999 'z)))))
