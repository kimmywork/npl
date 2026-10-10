; Bounded rectangular complex policy. Polar/transcendental operations remain open.
; The runtime canonicalizes exact-zero imaginary parts and mixed exactness.
(define (make-rectangular real imag)
  (if (if (real? real) (real? imag) #f) (npl_complex_make real imag)
      (argument-error "make-rectangular" "expected a real number"
                      (if (real? real) imag real))))
(define (real-part value)
  (if (number? value) (npl_complex_real value)
      (argument-error "real-part" "expected a number" value)))
(define (imag-part value)
  (if (number? value) (npl_complex_imag value)
      (argument-error "imag-part" "expected a number" value)))
