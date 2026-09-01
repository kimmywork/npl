(define-syntax with-temporary
  (syntax-rules ()
    ((_ value)
     ((lambda (temporary) (+ temporary 1)) value))))

(define differential-wind-log 0)

(define (differential-wind-digit digit)
  (set! differential-wind-log
        (+ (* differential-wind-log 10) digit)))

(define (differential-normal-wind)
  (set! differential-wind-log 0)
  (let ((value
          (dynamic-wind
            (lambda () (differential-wind-digit 1))
            (lambda () (differential-wind-digit 2) 42)
            (lambda () (differential-wind-digit 3)))))
    (+ (* value 1000) differential-wind-log)))

(define (differential-wind-exit)
  (set! differential-wind-log 0)
  (let ((value
          (call/cc
            (lambda (escape)
              (dynamic-wind
                (lambda () (differential-wind-digit 1))
                (lambda ()
                  (differential-wind-digit 2)
                  (escape 42))
                (lambda () (differential-wind-digit 3)))))))
    (+ (* value 1000) differential-wind-log)))

(define (differential-parameter)
  (let ((parameter (make-parameter 1)))
    (+ (parameterize ((parameter 41)) (parameter))
       (parameter))))

(define (differential-raise-wind)
  (set! differential-wind-log 0)
  (let ((value
          (call/cc
            (lambda (escape)
              (with-exception-handler
                (lambda (object) (escape object))
                (lambda ()
                  (dynamic-wind
                    (lambda () (differential-wind-digit 1))
                    (lambda () (raise 42))
                    (lambda () (differential-wind-digit 2)))))))))
    (+ (* value 100) differential-wind-log)))

(define differential-promise-count 0)

(define (differential-promise-memoization)
  (set! differential-promise-count 0)
  (let ((promise
          (delay
            (begin
              (set! differential-promise-count
                    (+ differential-promise-count 1))
              42))))
    (+ (+ (force promise) (force promise))
       differential-promise-count)))

(define (differential-promise-parameter)
  (let ((parameter (make-parameter 1)))
    (let ((promise (delay (parameter))))
      (parameterize ((parameter 42)) (force promise))
      (parameterize ((parameter 10)) (force promise)))))

(define (differential-promise-chain count)
  (if (= count 0)
      (delay 42)
      (delay-force (differential-promise-chain (- count 1)))))

(define (differential-results)
  (list
    (+ 20 22)
    (((lambda (left) (lambda (right) (+ left right))) 20) 22)
    (call-with-values
      (lambda () (values 20 22))
      (lambda (left right) (+ left right)))
    (+ 2 (call/cc (lambda (escape) (escape 40))))
    (with-temporary 41)
    (and #t (or #f 42))
    (differential-normal-wind)
    (differential-wind-exit)
    (with-exception-handler
      (lambda (object) (+ object 2))
      (lambda () (raise-continuable 40)))
    (differential-parameter)
    (differential-raise-wind)
    (differential-promise-memoization)
    (differential-promise-parameter)
    (force (differential-promise-chain 1000))))
