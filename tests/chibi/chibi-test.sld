;; Minimal portable (chibi test) for running chibi-scheme's
;; tests/r7rs-tests.scm on other implementations. Comparison follows
;; chibi's test-equal? (equal?, or approximate within 1e-5 for inexact
;; expected reals, componentwise for complex). Each test catches errors, so
;; one failure does not stop the run; FAIL lines and a summary go to
;; stdout.
(define-library (chibi test)
  (export test test-assert test-values test-error test-begin test-end
          test-exit)
  (import (scheme base) (scheme write) (scheme complex)
          (scheme process-context))
  (begin
    (define passed 0)
    (define failed 0)
    (define group "")
    (define (approx-equal? a b epsilon)
      (cond ((> (abs a) (abs b)) (approx-equal? b a epsilon))
            ((zero? a) (< (abs b) epsilon))
            (else (< (abs (/ (- a b) b)) epsilon))))
    (define (test-equal? expect result)
      (or (equal? expect result)
          (if (real? expect)
              (and (inexact? expect) (real? result)
                   (approx-equal? expect result 1e-5))
              (and (number? result) (number? expect)
                   (test-equal? (real-part expect) (real-part result))
                   (test-equal? (imag-part expect) (imag-part result))))))
    (define (report-failure name expect result)
      (set! failed (+ failed 1))
      (display "FAIL [") (display group) (display "] ")
      (write name) (display ": expected ") (write expect)
      (display " got ") (write result) (newline))
    (define (run name expect thunk compare)
      (let ((outcome (call-with-current-continuation
                       (lambda (k)
                         (with-exception-handler
                           (lambda (e) (k (list 'error e)))
                           (lambda () (list 'value (thunk))))))))
        (if (and (eq? (car outcome) 'value)
                 (compare expect (cadr outcome)))
            (set! passed (+ passed 1))
            (report-failure name expect
                            (if (eq? (car outcome) 'error)
                                (list 'raised
                                      (if (error-object? (cadr outcome))
                                          (error-object-message (cadr outcome))
                                          (cadr outcome)))
                                (cadr outcome))))))
    (define-syntax test
      (syntax-rules ()
        ((_ name expect expr)
         (run 'name expect (lambda () expr) test-equal?))
        ((_ expect expr)
         (run 'expr expect (lambda () expr) test-equal?))))
    (define-syntax test-assert
      (syntax-rules ()
        ((_ name expr) (run name #t (lambda () (if expr #t #f)) eq?))
        ((_ expr) (run 'expr #t (lambda () (if expr #t #f)) eq?))))
    (define-syntax test-values
      (syntax-rules ()
        ((_ expect expr)
         (run 'expr (call-with-values (lambda () expect) list)
              (lambda () (call-with-values (lambda () expr) list))
              test-equal?))))
    (define-syntax test-error
      (syntax-rules ()
        ((_ expr)
         (run 'expr 'error
              (lambda ()
                (call-with-current-continuation
                  (lambda (k)
                    (with-exception-handler
                      (lambda (e) (k 'error))
                      (lambda () expr 'no-error)))))
              eq?))))
    (define (test-begin . name) (if (pair? name) (set! group (car name))))
    (define (test-end . name) #f)
    (define (test-exit)
      (display "PASSED ") (display passed)
      (display " FAILED ") (display failed) (newline))))
