; Shared nplc / npli behavior for SRFI 230.  Every observation is
; deterministic and single-threaded; the C harness and native unit tests own
; contention and collector races.
(define (report name value) (write (list name value)) (newline))

(report 'orders
        (list (memory-order relaxed)
              (memory-order acquire)
              (memory-order release)
              (memory-order acquire-release)
              (memory-order sequentially-consistent)))
(report 'memory-order?
        (list (memory-order? 'relaxed)
              (memory-order? 'sequentially-consistent)
              (memory-order? 'bogus)))

(let ((flag (make-atomic-flag)))
  (report 'flag? (atomic-flag? flag))
  (report 'flag-first (atomic-flag-test-and-set! flag))
  (report 'flag-second
          (atomic-flag-test-and-set! flag (memory-order acquire)))
  (atomic-flag-clear! flag (memory-order release))
  (report 'flag-cleared (atomic-flag-test-and-set! flag)))

(let ((box (make-atomic-box 'a)))
  (report 'box? (atomic-box? box))
  (report 'box-ref (atomic-box-ref box (memory-order sequentially-consistent)))
  (report 'box-swap (atomic-box-swap! box 'b (memory-order release)))
  (report 'box-cas-failed
          (atomic-box-compare-and-swap! box 'a 'c))
  (report 'box-cas-succeeded
          (atomic-box-compare-and-swap! box 'b 'c))
  (atomic-box-set! box 'd)
  (report 'box-final (atomic-box-ref box)))

(let ((box (make-atomic-fxbox 10)))
  (report 'fxbox? (atomic-fxbox? box))
  (report 'fxbox-ref (atomic-fxbox-ref box))
  (report 'fxbox-swap (atomic-fxbox-swap! box 20))
  (report 'fxbox-cas-failed
          (atomic-fxbox-compare-and-swap! box 10 30))
  (report 'fxbox-cas-succeeded
          (atomic-fxbox-compare-and-swap! box 20 30))
  (report 'fxbox-add (atomic-fxbox+/fetch! box 5))
  (report 'fxbox-sub (atomic-fxbox-/fetch! box 2))
  (report 'fxbox-and (atomic-fxbox-and/fetch! box 15))
  (report 'fxbox-ior (atomic-fxbox-ior/fetch! box 6))
  (report 'fxbox-xor (atomic-fxbox-xor/fetch! box 3))
  (report 'fxbox-final (atomic-fxbox-ref box)))

(let ((pair (make-atomic-pair 'a 'b)))
  (report 'pair? (atomic-pair? pair))
  (report 'pair-ref
          (call-with-values (lambda () (atomic-pair-ref pair)) list))
  (report 'pair-swap
          (call-with-values
            (lambda () (atomic-pair-swap! pair 'c 'd)) list))
  (report 'pair-cas-failed
          (call-with-values
            (lambda ()
              (atomic-pair-compare-and-swap! pair 'a 'b 'e 'f))
            list))
  (report 'pair-cas-succeeded
          (call-with-values
            (lambda ()
              (atomic-pair-compare-and-swap! pair 'c 'd 'e 'f))
            list))
  (report 'pair-final
          (call-with-values (lambda () (atomic-pair-ref pair)) list)))

(report 'fence
        (begin
          (atomic-fence (memory-order sequentially-consistent))
          #t))
