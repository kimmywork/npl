(define (report name value) (write (list name value)) (newline))
(define (complex-results)
  (report 'construction
    (list (make-rectangular 1.0 2.0)
          (make-rectangular 0.5 -0.6666666666666666)
          1.0+2.0i))
  (report 'arithmetic
    (list (+ 1.0+2.0i 3.0+4.0i)
          (- 1.0+2.0i 3.0+4.0i)
          (* 1.0+2.0i 3.0+4.0i)
          (/ 1.0+2.0i 3.0+4.0i)))
  (report 'components
    (list (real-part 1.0+2.0i) (imag-part 1.0+2.0i)
          (complex? 1.0+2.0i) (real? 1.0+2.0i)
          (rational? 1.0+2.0i)))
  (report 'conversion
    (list (string->number "1.0+2.0i")
          (number->string 1.0+2.0i)
          (eqv? 1.0+2.0i (string->number "1.0+2.0i")))))
(define fixture-result (begin (complex-results) 0))
