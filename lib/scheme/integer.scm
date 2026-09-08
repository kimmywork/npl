; Public exact-integer arithmetic policy; both native and evaluator execute it.
; Raw primitives remain the compiler/runtime boundary, not a second public API.
(define (check-numeric-arguments who xs)
  (if (not (null? xs))
      (if (number? (car xs))
          (check-numeric-arguments who (cdr xs))
          (raise (string-append who ": expected numbers")))))

(define (sum-integers xs value)
  (if (null? xs) value
      (sum-integers (cdr xs) (raw-add value (car xs)))))
(define (multiply-integers xs value)
  (if (null? xs) value
      (multiply-integers (cdr xs) (raw-multiply value (car xs)))))
(define (subtract-integers xs value)
  (if (null? xs) value
      (subtract-integers (cdr xs) (raw-subtract value (car xs)))))
(define (integer-add . xs)
  (check-numeric-arguments "+" xs)
  (sum-integers xs 0))
(define (integer-multiply . xs)
  (check-numeric-arguments "*" xs)
  (multiply-integers xs 1))
(define (integer-subtract . xs)
  (if (null? xs) (raise "-: needs an argument"))
  (check-numeric-arguments "-" xs)
  (if (null? (cdr xs)) (raw-subtract 0 (car xs))
      (subtract-integers (cdr xs) (car xs))))

; Use a small operation code rather than exposing raw primitives as closures.
(define (compare-integer-pair operation a b)
  (cond ((raw-equal operation 0) (raw-equal a b))
        ((raw-equal operation 1) (raw-less a b))
        ((raw-equal operation 2) (raw-less-equal a b))
        ((raw-equal operation 3) (raw-greater a b))
        (else (raw-greater-equal a b))))
(define (compare-integer-chain operation xs)
  (if (null? (cdr xs)) #t
      (if (compare-integer-pair operation (car xs) (car (cdr xs)))
          (compare-integer-chain operation (cdr xs)) #f)))
(define (compare-integers who operation xs)
  (if (if (null? xs) #t (null? (cdr xs)))
      (raise (string-append who ": needs at least two arguments")))
  ; Validate all arguments, including those beyond a false adjacent comparison.
  (check-numeric-arguments who xs)
  (compare-integer-chain operation xs))
(define (integer-equal . xs) (compare-integers "=" 0 xs))
(define (integer-less . xs) (compare-integers "<" 1 xs))
(define (integer-less-equal . xs) (compare-integers "<=" 2 xs))
(define (integer-greater . xs) (compare-integers ">" 3 xs))
(define (integer-greater-equal . xs) (compare-integers ">=" 4 xs))

(define (number->string value)
  (if (number? value) (raw-number->string value)
      (raise "number->string: expected a number")))
