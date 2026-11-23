; Shared test-framework core.  Keep this file dependent only on bindings the
; compiler exposes in its default environment: historical suites include it
; without imports, and partial-import suites include it too.

(define TEST-RUNNER-ACTIVE #f)
(define TEST-NAME "test")
(define TEST-TOTAL 0)
(define TEST-PASS 0)
(define TEST-FAIL 0)
(define TEST-SKIP 0)
(define TEST-XFAIL 0)
(define TEST-EXPECT 0)
(define TEST-SKIP-NEXT 0)

(define (ensure-runner)
  (set! TEST-RUNNER-ACTIVE #t)
  #t)

(define (test-runner? value)
  (if (eq? value 'test-runner) #t #f))

(define (test-runner-current)
  (if TEST-RUNNER-ACTIVE 'test-runner #f))

(define (test-runner-factory)
  'test-runner)

(define (test-runner-simple) 'test-runner)
(define (test-runner-null) 'test-runner)

(define (test-runner-reset runner)
  (set! TEST-TOTAL 0)
  (set! TEST-PASS 0)
  (set! TEST-FAIL 0)
  (set! TEST-SKIP 0)
  (set! TEST-XFAIL 0)
  runner)

(define (test-runner-pass-count . rest) TEST-PASS)
(define (test-runner-fail-count . rest) TEST-FAIL)
(define (test-runner-skip-count . rest) TEST-SKIP)
(define (test-runner-xfail-count . rest) TEST-XFAIL)
(define (test-runner-total-count . rest) TEST-TOTAL)
(define (test-runner-result . rest) '())

(define (test-record! name passed . rest)
  (ensure-runner)
  (set! TEST-TOTAL (+ TEST-TOTAL 1))
  (if (= TEST-SKIP-NEXT 0)
      (if (= TEST-EXPECT 0)
          (if passed
              (set! TEST-PASS (+ TEST-PASS 1))
              (begin
                (set! TEST-FAIL (+ TEST-FAIL 1))
                (display "FAIL ")
                (display name)
                (if (null? rest)
                    #f
                    (begin
                      (display ": ")
                      (display (car rest))))
                (newline)))
          (begin
            (set! TEST-EXPECT (- TEST-EXPECT 1))
            (set! TEST-XFAIL (+ TEST-XFAIL 1))
            (if passed
                (begin
                  (set! TEST-FAIL (+ TEST-FAIL 1))
                  (display "FAIL ")
                  (display name)
                  (display ": expected failure, got pass")
                  (newline))
                (set! TEST-PASS (+ TEST-PASS 1)))))
      (begin
        (set! TEST-SKIP-NEXT (- TEST-SKIP-NEXT 1))
        (set! TEST-SKIP (+ TEST-SKIP 1))
        (display "SKIP ")
        (display name)
        (newline))))

(define (test-record-skip! name)
  (ensure-runner)
  (set! TEST-TOTAL (+ TEST-TOTAL 1))
  (set! TEST-SKIP (+ TEST-SKIP 1))
  (display "SKIP ")
  (display name)
  (newline))

(define (test-expect-fail! count)
  (set! TEST-EXPECT (+ TEST-EXPECT count))
  count)

(define (test-skip! count)
  (set! TEST-SKIP-NEXT (+ TEST-SKIP-NEXT count))
  count)

(define (%test-begin name)
  (ensure-runner)
  (set! TEST-NAME name)
  #t)

(define (%test-end name)
  (if name (set! TEST-NAME name))
  #t)

(define (%test-assert name thunk)
  (guard (condition
          (#t (test-record! name #f "raised an exception")))
    (test-record! name (if (thunk) #t #f) "")))

(define (test-value-equal? actual expected)
  (if (eqv? actual expected)
      #t
      (if (if (string? actual) (string? expected) #f)
          (string=? actual expected)
          #f)))

(define (%test-equal name thunk expected)
  (guard (condition
          (#t (test-record! name #f "raised an exception")))
    (test-record! name (test-value-equal? (thunk) expected) "values differ")))

(define (%test-eqv name thunk expected)
  (guard (condition
          (#t (test-record! name #f "raised an exception")))
    (test-record! name (eqv? (thunk) expected) "values differ")))

(define (%test-eq name thunk expected)
  (guard (condition
          (#t (test-record! name #f "raised an exception")))
    (test-record! name (eq? (thunk) expected) "values differ")))

(define (%test-inexact name thunk expected)
  (guard (condition
          (#t (test-record! name #f "raised an exception")))
    (test-record! name (= (thunk) expected) "values differ")))

(define (%test-error name thunk)
  (guard (condition (#t (test-record! name #t "")))
    (thunk)
    (test-record! name #f "expected an error")))

(define (%test-group name thunk)
  (let ((saved-name TEST-NAME)
        (saved-total TEST-TOTAL)
        (saved-pass TEST-PASS)
        (saved-fail TEST-FAIL)
        (saved-skip TEST-SKIP)
        (saved-xfail TEST-XFAIL)
        (saved-expect TEST-EXPECT)
        (saved-skip-next TEST-SKIP-NEXT))
    (set! TEST-NAME name)
    (set! TEST-TOTAL 0)
    (set! TEST-PASS 0)
    (set! TEST-FAIL 0)
    (set! TEST-SKIP 0)
    (set! TEST-XFAIL 0)
    (guard (condition
            (#t (test-record! name #f "raised an exception")))
      (thunk))
    (set! TEST-TOTAL (+ saved-total TEST-TOTAL))
    (set! TEST-PASS (+ saved-pass TEST-PASS))
    (set! TEST-FAIL (+ saved-fail TEST-FAIL))
    (set! TEST-SKIP (+ saved-skip TEST-SKIP))
    (set! TEST-XFAIL (+ saved-xfail TEST-XFAIL))
    (set! TEST-EXPECT saved-expect)
    (set! TEST-SKIP-NEXT saved-skip-next)
    (set! TEST-NAME saved-name)
    #t))

(define (finish-tests name)
  (set! TEST-NAME name)
  (if (= TEST-FAIL 0)
      (begin
        (display "PASS ")
        (display name)
        (display " ")
        (display TEST-TOTAL)
        (newline)
        (exit 0))
      (begin
        (display "FAILED ")
        (display name)
        (display " ")
        (display TEST-FAIL)
        (display "/")
        (display TEST-TOTAL)
        (newline)
        (exit 1))))

(define-syntax test-begin
  (syntax-rules ()
    ((_) (%test-begin "test"))
    ((_ name) (%test-begin name))))

(define-syntax test-end
  (syntax-rules ()
    ((_) (%test-end #f))
    ((_ name) (%test-end name))))

(define-syntax test-assert
  (syntax-rules ()
    ((_ expr) (%test-assert 'expr (lambda () expr)))
    ((_ name expr) (%test-assert name (lambda () expr)))))

(define-syntax test-equal
  (syntax-rules ()
    ((_ name (lambda formals body ...) expected)
     (%test-equal name (lambda formals body ...) expected))
    ((_ expected expr) (%test-equal 'test-equal (lambda () expr) expected))
    ((_ name expected expr) (%test-equal name (lambda () expr) expected))))

(define-syntax test-eqv
  (syntax-rules ()
    ((_ name (lambda formals body ...) expected)
     (%test-eqv name (lambda formals body ...) expected))
    ((_ expected expr) (%test-eqv 'test-eqv (lambda () expr) expected))
    ((_ name expected expr) (%test-eqv name (lambda () expr) expected))))

(define-syntax test-eq
  (syntax-rules ()
    ((_ name (lambda formals body ...) expected)
     (%test-eq name (lambda formals body ...) expected))
    ((_ expected expr) (%test-eq 'test-eq (lambda () expr) expected))
    ((_ name expected expr) (%test-eq name (lambda () expr) expected))))

(define-syntax test-inexact
  (syntax-rules ()
    ((_ name (lambda formals body ...) expected)
     (%test-inexact name (lambda formals body ...) expected))
    ((_ expected expr)
     (%test-inexact 'test-inexact (lambda () expr) expected))
    ((_ name expected expr)
     (%test-inexact name (lambda () expr) expected))))

(define-syntax test-identity
  (syntax-rules ()
    ((_ name thunk expected) (%test-eq name thunk expected))))

(define-syntax test-error
  (syntax-rules ()
    ((_ expr) (%test-error 'test-error (lambda () expr)))
    ((_ name expr) (%test-error name (lambda () expr)))
    ((_ name error-type expr) (%test-error name (lambda () expr)))))

(define-syntax test-expect-fail
  (syntax-rules ()
    ((_ count) (test-expect-fail! count))))

(define-syntax test-skip
  (syntax-rules ()
    ((_ count) (test-skip! count))))

(define-syntax test-group
  (syntax-rules ()
    ((_ name body ...) (%test-group name (lambda () body ...)))))

(define-syntax test-group-with-cleanup
  (syntax-rules ()
    ((_ name body ...)
     (%test-group name (lambda () body ...)))))
