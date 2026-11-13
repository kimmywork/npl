; Review test harness. Each check runs its thunk under an exception handler,
; compares with equal?, and reports one line per failure so a single run
; lists every deviation instead of stopping at the first error.
;
; Test names start with a case ID from ../2026-11-12-srfi-i2-i3-test-design.md.
; IDs listed in KNOWN-FAILURES there reproduce review findings.

(define review-run 0)
(define review-failed 0)

(define (review-capture thunk)
  (call/cc
   (lambda (k)
     (with-exception-handler
      (lambda (e)
        (k (cons 'raised
                 (if (error-object? e) (error-object-message e) e))))
      (lambda () (cons 'value (thunk)))))))

(define (review-fail name detail)
  (set! review-failed (+ review-failed 1))
  (display "FAIL ")
  (display name)
  (display " :: ")
  (write detail)
  (newline))

(define (check name thunk expected)
  (set! review-run (+ review-run 1))
  (let ((result (review-capture thunk)))
    (if (not (and (eq? (car result) 'value)
                  (equal? (cdr result) expected)))
        (review-fail name (cons 'got (cons result (cons 'expected
                                                        (cons expected '()))))))))

(define (check-true name thunk)
  (check name (lambda () (if (thunk) #t #f)) #t))

(define (check-error name thunk)
  (set! review-run (+ review-run 1))
  (let ((result (review-capture thunk)))
    (if (not (eq? (car result) 'raised))
        (review-fail name (cons 'expected-error (cons result '()))))))

(define (values->list thunk)
  (call-with-values thunk (lambda items items)))

(define (mutable-string? s)
  (eq? (car (review-capture (lambda () (string-fill! s #\x 0 0) #t)))
       'value))

(define (review-finish suite)
  (display (if (= review-failed 0) "PASS " "FAILED "))
  (display suite)
  (display " ")
  (if (> review-failed 0)
      (begin (display review-failed) (display "/")))
  (display review-run)
  (newline)
  (exit (if (= review-failed 0) 0 1)))
