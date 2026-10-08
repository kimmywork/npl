; Bounded rectangular complex policy. Polar/transcendental operations remain open.
; The runtime canonicalizes exact-zero imaginary parts and mixed exactness.
(define (make-rectangular real imag)
  (if (if (real? real) (real? imag) #f) (npl_complex_make real imag)
      (raise "make-rectangular: expected real numbers")))
(define (real-part value)
  (if (number? value) (npl_complex_real value)
      (raise "real-part: expected a number")))
(define (imag-part value)
  (if (number? value) (npl_complex_imag value)
      (raise "imag-part: expected a number")))
