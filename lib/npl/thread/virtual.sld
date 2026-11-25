;; (npl thread virtual) — T2b JVM-style virtual threads (M:N substrate).
;;
;; Virtual threads are multiplexed over carrier pthreads by the runtime. A
;; virtual thread suspends only at a CPS boundary via virtual-thread-yield!;
;; its one-shot scheduler continuation and dynamic environment are heap
;; objects, so no carrier is blocked and no C stack frame is retained.
;; Native SRFI 18 threads remain 1:1 and coexist with virtual threads.
;;
;; virtual-thread-yield! is a control operator (like call/cc), not a C
;; primitive: it hands the raw current continuation to the scheduler. It is
;; imported from (npl internal primitive) and re-exported here.
(define-library (npl thread virtual)
  (export virtual-thread? make-virtual-thread virtual-thread-start!
          virtual-thread-yield! virtual-thread-join! current-virtual-thread)
  (import (scheme base)
          (only (npl internal primitive) virtual-thread-yield!))
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
      (%vthread-join thread))))
