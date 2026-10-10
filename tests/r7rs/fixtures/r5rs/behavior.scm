; (scheme r5rs) alone provides the R5RS procedures and syntax (R7RS
; Appendix A), and its environments evaluate R5RS programs (R7RS 6.12).
; (sqrt 16) is exact 4 in Guile and 4.0 in NPL (R7RS 6.2.6 allows both).
(define (report name value) (write (list name value)) (newline))

(define r1 (report 'procedures
  (list (+ 1 2) (cadddr '(1 2 3 4)) (exact->inexact 1/2) (inexact->exact 0.5)
        (string-ci=? "Ab" "aB") (char-upcase #\a) (magnitude -2)
        (sqrt 16.0) (string->symbol "s") (list-tail '(1 2 3) 2)
        (call-with-current-continuation (lambda (k) (k 'escaped))))))
(define r2 (report 'syntax
  (let loop ((i 0) (acc '()))
    (if (= i 3)
        (list acc (case 2 ((1) 'one) ((2) 'two) (else 'many))
              (do ((j 0 (+ j 1)) (s 0 (+ s j))) ((= j 4) s))
              `(1 ,(+ 1 1) ,@(list 3)))
        (loop (+ i 1) (cons i acc))))))
(define r3 (report 'promises (force (delay (* 6 7)))))
(define r4 (report 'values (call-with-values (lambda () (values 1 2)) +)))
(define r5 (report 'dynamic-wind
  (let ((log '()))
    (dynamic-wind (lambda () (set! log (cons 'in log)))
                  (lambda () 'body)
                  (lambda () (set! log (cons 'out log))))
    (reverse log))))
(define r6 (report 'scheme-report-environment
  (eval '(let ((x 6)) (* x 7)) (scheme-report-environment 5))))
(define r7 (report 'report-environment-procedures
  (eval '(cond ((assv 2 '((1 . a) (2 . b))) => cdr) (else 'none))
        (scheme-report-environment 5))))
(define r8 (report 'null-environment
  (eval '(if (and #t (or #f 1)) 'yes 'no) (null-environment 5))))
(define r9 (report 'string-ports
  (let ((port (open-output-file "/tmp/npl-fixture-r5rs.txt")))
    (write '(saved 1) port)
    (close-output-port port)
    (call-with-input-file "/tmp/npl-fixture-r5rs.txt" read))))
