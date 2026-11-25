;; (npl thread virtual) — T2b JVM-style virtual threads (M:N substrate).
;;
;; Virtual threads are multiplexed over carrier pthreads by the runtime. A
;; virtual thread suspends only at a CPS boundary via virtual-thread-yield!;
;; its one-shot scheduler continuation and dynamic environment are heap
;; objects, so no carrier is blocked and no C stack frame is retained.
;; Native SRFI 18 threads remain 1:1 and coexist with virtual threads.
;;
;; virtual-thread-yield! / virtual-thread-block! are control operators (like
;; call/cc), not C primitives: they hand the raw current continuation to the
;; scheduler. They are imported from (npl internal primitive) and re-exported
;; here. virtual-thread-block! runs a synchronous blocking thunk on a helper
;; worker and parks the virtual thread until the result is ready.
(define-library (npl thread virtual)
  (export virtual-thread? make-virtual-thread virtual-thread-start!
          virtual-thread-yield! virtual-thread-join! current-virtual-thread
          virtual-thread-block! virtual-thread-sleep!)
  (import (scheme base)
          (only (npl internal primitive)
                virtual-thread-yield! virtual-thread-block!)
          (only (npl thread) thread-sleep!))
  (begin
    (define-c-primitive (%vthread-make (thunk any) (name any) (params any)) any
      "npl_vthread_make")
    (define-c-primitive (virtual-thread? (value any)) any "npl_vthread_p")
    (define-c-primitive (%vthread-start (value any)) any "npl_vthread_start")
    (define-c-primitive (%vthread-join (value any)) any "npl_vthread_join")
    (define-c-primitive (current-virtual-thread) any "npl_vthread_current")
    (define-c-primitive (%vthread-current-parameters) any
      "npl_vthread_current_parameters")

    ;; Optional name stays NPL policy; C always receives fixed operands.
    (define (make-virtual-thread thunk . maybe-name)
      (%vthread-make thunk
                     (if (null? maybe-name) #f (car maybe-name))
                     (%vthread-current-parameters)))

    (define (virtual-thread-start! thread)
      (%vthread-start thread))

    (define (virtual-thread-join! thread)
      (%vthread-join thread))

    ;; Synchronous SRFI 18 sleep would block a whole carrier. Wrapping it
    ;; in virtual-thread-block! moves the wait to a helper worker and yields
    ;; the virtual thread instead. The same wrapper covers mutex/condition
    ;; waits and blocking I/O thunks.
    (define (virtual-thread-sleep! timeout)
      (virtual-thread-block!
        (lambda () (thread-sleep! timeout))))))
