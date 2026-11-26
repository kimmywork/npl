; Deterministic SRFI 226 control prototype fixture.  The nplc and npli
; surfaces must produce identical values; the helper-thread composable path
; has no visible scheduling output.
(define (report name value) (write (list name value)) (newline))

(define tag (make-continuation-prompt-tag 'fixture))

(report 'prompt
        (call-with-continuation-prompt (lambda () (+ 1 2)) tag))
(report 'abort
        (call-with-continuation-prompt
          (lambda () (abort-current-continuation tag 'a 'b))
          tag
          list))
(report 'default-handler
        (call-with-continuation-prompt
          (lambda ()
            (abort-current-continuation
              tag
              (lambda ()
                (abort-current-continuation tag (lambda () 27)))))
          tag))
(report 'reset
        (list (+ 1 (reset 3))
              (+ 1 (reset (* 2 (shift k 4))))
              (+ 1 (reset (* 2 (shift k (k 4)))))
              (+ 1 (reset (* 2 (shift k (k (k 4))))))))
(report 'nested
        (+ 1 (reset (* 2 (shift k1 (* 3 (shift k2 (k1 (k2 4)))))))))
(report 'reset-at
        (let ((at (make-continuation-prompt-tag 'at)))
          (+ 1 (reset-at at (* 2 (shift-at at k 4))))))
(report 'composable
        (let ((prompt (make-continuation-prompt-tag 'composable)))
          (* 2
             (call-with-continuation-prompt
               (lambda ()
                 (* 3
                    (call-with-composable-continuation
                      (lambda (k)
                        (* 5
                           (call-with-continuation-prompt
                             (lambda () (* 7 (k 11)))
                             prompt)))
                      prompt)))
               prompt))))
(report 'non-composable
        (let ((prompt (make-continuation-prompt-tag 'non-composable)))
          (* 2
             (call-with-continuation-prompt
               (lambda ()
                 (* 3
                    (call-with-non-composable-continuation
                      (lambda (k)
                        (* 5
                           (call-with-continuation-prompt
                             (lambda () (* 7 (k 11)))
                             prompt)))
                      prompt)))
               prompt))))
(report 'predicates
        (let ((prompt (make-continuation-prompt-tag 'predicates)))
          (call-with-continuation-prompt
            (lambda ()
              (let ((composable
                     (call-with-composable-continuation
                       (lambda (k) k)
                       prompt))
                    (full (call/cc (lambda (k) k))))
                (list (continuation? composable)
                      (non-composable-continuation? composable)
                      (continuation? full)
                      (non-composable-continuation? full))))
            prompt)))
(report 'prompt-available
        (let ((prompt (make-continuation-prompt-tag 'available)))
          (call-with-continuation-prompt
            (lambda ()
              (list
                (continuation-prompt-available? prompt)
                (continuation-prompt-available?
                  prompt
                  (call-with-non-composable-continuation values prompt))
                (continuation-prompt-available?
                  prompt
                  (call-with-composable-continuation values prompt))))
            prompt)))
(report 'multiple-values
        (call-with-continuation-prompt
          (lambda ()
            (call-with-values
              (lambda ()
                (call-with-composable-continuation
                  (lambda (k)
                    (abort-current-continuation tag (lambda () (k 1 2))))
                  tag))
              list))
          tag))
(report 'unwind-protect
        (let ((prompt (make-continuation-prompt-tag 'unwind))
              (cleaned #f))
          (call-with-continuation-prompt
            (lambda ()
              (unwind-protect
                (abort-current-continuation prompt 'aborted)
                (set! cleaned #t)))
            prompt
            (lambda (value) (list value cleaned)))))
(report 'missing-prompt
        (guard (e ((continuation-violation? e)
                   (eq? (continuation-violation-prompt-tag e) tag)))
          (abort-current-continuation tag)))
