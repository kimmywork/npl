(define-library (scheme complex)
  (export angle imag-part magnitude make-polar make-rectangular real-part)
  (import (scheme base) (scheme inexact))
  (begin
    ; Rectangular parts come from (scheme base); polar forms are built on
    ; (scheme inexact). A nonnegative exact real has angle 0 (Racket; Guile
    ; answers 0.0), a negative real pi.
    (define (magnitude z)
      (if (real? z)
          (abs z)
          (let ((x (real-part z)) (y (imag-part z)))
            (sqrt (+ (* x x) (* y y))))))
    ; real-part rejects a non-number with the standard argument error.
    (define (angle z)
      (let ((x (real-part z)))
        (if (real? z)
            (if (negative? x) (atan 0 -1) (if (exact? x) 0 0.0))
            (atan (imag-part z) x))))
    ; An exact zero angle gives the magnitude itself (as Racket and Guile).
    (define (make-polar magnitude angle)
      (if (if (exact? angle) (zero? angle) #f)
          (+ magnitude 0)
          (make-rectangular (* magnitude (cos angle))
                            (* magnitude (sin angle)))))))
