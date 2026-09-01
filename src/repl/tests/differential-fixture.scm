(define-syntax with-temporary
  (syntax-rules ()
    ((_ value)
     ((lambda (temporary) (+ temporary 1)) value))))

(define (differential-results)
  (list
    (+ 20 22)
    (((lambda (left) (lambda (right) (+ left right))) 20) 22)
    (call-with-values
      (lambda () (values 20 22))
      (lambda (left right) (+ left right)))
    (+ 2 (call/cc (lambda (escape) (escape 40))))
    (with-temporary 41)
    (and #t (or #f 42))))
