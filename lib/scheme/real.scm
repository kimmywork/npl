; Real-number utilities of (scheme base) layered on the shared exactness policy.

; min/max: any inexact argument makes the result inexact; NaN propagates.
(define (extremum-loop maximum? xs best inexact)
  (if (null? xs) (inexact-if inexact best)
      (let ((x (car xs)))
        (extremum-loop maximum? (cdr xs)
          (cond ((npl_nan_p best) best)
                ((npl_nan_p x) x)
                ((if maximum? (raw-greater x best) (raw-less x best)) x)
                (else best))
          (if inexact #t (npl_inexact_p x))))))
(define (extremum who maximum? xs)
  (if (null? xs) (argument-error who "wrong number of arguments" 0))
  (check-real-arguments who xs)
  (extremum-loop maximum? (cdr xs) (car xs) (npl_inexact_p (car xs))))
(define (max . xs) (extremum "max" #t xs))
(define (min . xs) (extremum "min" #f xs))

; Newton iteration from a decimal-digit upper bound on the square root.
(define (integer-sqrt-loop n x)
  (let ((y (raw-quotient (raw-add x (raw-quotient n x)) 2)))
    (if (raw-less y x) (integer-sqrt-loop n y) x)))
(define (integer-sqrt n)
  (if (raw-less n 2) n
      (let ((digits (string-length (raw-number->string n))))
        (integer-sqrt-loop n (power-loop 10 (raw-quotient (raw-add digits 1) 2) 1)))))
(define (exact-integer-sqrt k)
  (if (if (npl_integer_p k) (not (raw-less k 0)) #f)
      (let ((s (integer-sqrt k)))
        (values s (raw-subtract k (raw-multiply s s))))
      (argument-error "exact-integer-sqrt"
                      "expected an exact nonnegative integer" k)))

; Simplest rational in a closed interval (Stern-Brocot descent), exact inputs.
(define (simplest-positive low high)
  (let ((whole (exact-floor low)))
    (cond ((raw-equal whole low) whole)
          ((raw-equal whole (exact-floor high))
           (raw-add whole
             (npl_number_div 1
               (simplest-positive
                 (npl_number_div 1 (raw-subtract high whole))
                 (npl_number_div 1 (raw-subtract low whole))))))
          (else (raw-add whole 1)))))
(define (simplest-rational low high)
  (cond ((raw-greater low 0) (simplest-positive low high))
        ((raw-less high 0)
         (raw-subtract 0 (simplest-positive (raw-subtract 0 high)
                                            (raw-subtract 0 low))))
        (else 0)))
(define (rationalize x y)
  (if (if (real? x) (real? y) #f)
      (let ((inexact (if (npl_inexact_p x) #t (npl_inexact_p y))))
        (cond ((if (npl_nan_p x) #t (npl_nan_p y)) (inexact-if #t (if (npl_nan_p x) x y)))
              ((npl_infinite_p y) (if (npl_infinite_p x) (raw-subtract x x) 0.0))
              ((npl_infinite_p x) x)
              (else
               (let ((center (exact x))
                     (radius (abs (exact y))))
                 (inexact-if inexact
                   (simplest-rational (raw-subtract center radius)
                                      (raw-add center radius)))))))
      (argument-error "rationalize" "expected a real number"
                      (if (real? x) y x))))
