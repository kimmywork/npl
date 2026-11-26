(define (run)
  (define (report name value) (write (list name value)) (newline))

  (define p (make-parameter 10 (lambda (x) (* x x))))
  (define saved #f)
  (report 'parameter? (list (parameter? p) (parameter? 1)))
  (report 'initial (p))
  (p 12)
  (report 'assigned (p))
  (report 'parameterize
          (parameterize ((p (p)))
            (set! saved (current-parameterization))
            (p)))
  (report 'parameterization? (parameterization? saved))
  (report 'call-with-parameterization
          (call-with-parameterization
            saved
            (lambda ()
              (let ((x (p)))
                (p 0)
                (list x (p))))))
  (report 'restored (p))
  (report 'temporarily
          (let ((q (make-parameter 1 (lambda (x) (+ x 1)))))
            (temporarily ((q 4)) (values))
            (q)))
  (report 'temporarily-multiple
          (let ((a (make-parameter 1))
                (b (make-parameter 2)))
            (list (temporarily ((a 10) (b 20))
                    (list (a) (b)))
                  (a)
                  (b))))
)
(run)
