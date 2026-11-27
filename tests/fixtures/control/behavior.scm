; Deterministic SRFI 226 control prototype fixture.  The nplc and npli
; surfaces must produce identical values; prompt segments and barrier/call-in
; frames have no visible scheduling output.
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

(report 'barrier-escape
        (call/cc
          (lambda (continuation)
            (call-with-continuation-barrier
              (lambda () (continuation 'ok))))))
(report 'barrier-inward
        (guard (e ((continuation-violation? e) #t)
                  (else #f))
          ((call-with-continuation-barrier
             (lambda () (call/cc values))))))
(report 'barrier-composable
        (guard (e ((continuation-violation? e) #t)
                  (else #f))
          (call-with-continuation-prompt
            (lambda ()
              (call-with-continuation-barrier
                (lambda ()
                  (call-with-composable-continuation values tag))))
            tag)))
(report 'call-in
        (let ((prompt (make-continuation-prompt-tag 'call-in)))
          (call-with-continuation-prompt
            (lambda ()
              (call/cc
                (lambda (continuation)
                  (call-in-continuation
                    continuation (lambda (x) (* x 2)) 21))))
            prompt)))
(report 'return-to
        (let ((prompt (make-continuation-prompt-tag 'return-to)))
          (call-with-continuation-prompt
            (lambda ()
              (call/cc (lambda (continuation)
                         (return-to continuation 7))))
            prompt)))
(report 'call-in-values
        (let ((prompt (make-continuation-prompt-tag 'call-in-values)))
          (call-with-values
            (lambda ()
              (call-with-continuation-prompt
                (lambda ()
                  (call/cc
                    (lambda (continuation)
                      (call-in-continuation
                        continuation (lambda () (values 1 2))))))
                prompt))
            list)))

;; Cross the compiled/evaluated boundary in both directions.  The runtime
;; continuation object is shared, so the host's predicates and call-in see
;; the same continuation the evaluated library produced (and vice versa).
(define interop-environment
  (environment '(except (scheme base) call/cc call-with-current-continuation)
               '(npl control)))
(define interop-tag (make-continuation-prompt-tag 'interop))
(define interop-box (vector #f))

(report 'evaluated-capture
        (call-with-continuation-prompt
          (lambda ()
            (let ((proc
                   (eval
                     '(lambda (box tag)
                        (call-with-composable-continuation
                          (lambda (k) (vector-set! box 0 k) 0)
                          tag))
                     interop-environment)))
              (proc interop-box interop-tag)))
          interop-tag))
(report 'evaluated-capture-kind
        (continuation? (vector-ref interop-box 0)))
(report 'native-invokes-evaluated-capture
        (call-with-continuation-prompt
          (lambda ()
            (call-in-continuation
              (vector-ref interop-box 0) (lambda (x) x) 10))
          interop-tag))

(define interop-native-k #f)
(report 'native-capture
        (call-with-continuation-prompt
          (lambda ()
            (+ 1 (call-with-composable-continuation
                   (lambda (k) (set! interop-native-k k) 0)
                   interop-tag)))
          interop-tag))
(report 'evaluated-invokes-native-capture
        ((eval
           '(lambda (k tag)
              (call-with-continuation-prompt
                (lambda () (call-in-continuation k (lambda (x) x) 10))
                tag))
           interop-environment)
         interop-native-k interop-tag))
(report 'missing-prompt
        (guard (e ((continuation-violation? e)
                   (eq? (continuation-violation-prompt-tag e) tag)))
          (abort-current-continuation tag)))
