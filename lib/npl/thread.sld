;; (npl thread) — SRFI 18 multithreading support, 1:1 pthread substrate.
;;
;; Runtime C owns pthreads, stacks, GC roots and safepoints; this library is
;; the public NPL boundary. Users never see pthread_t. The scheduler
;; interface (create/start/join/yield/block/terminate) is the only place that
;; knows the substrate is 1:1.
;;
;; SRFI 18 has no condition-variable-wait!: waiting is mutex-unlock! with a
;; condition variable followed by an explicit mutex-lock!. It imposes no
;; fairness constraints. current-time/time? here are SRFI 18 time objects,
;; distinct from SRFI 19's (npl time) time structures.
(define-library (npl thread)
  (export current-thread thread? make-thread thread-name thread-specific
          thread-specific-set! thread-start! thread-yield! thread-sleep!
          thread-terminate! thread-join!
          mutex? make-mutex mutex-name mutex-specific mutex-specific-set!
          mutex-state mutex-lock! mutex-unlock!
          condition-variable? make-condition-variable
          condition-variable-name condition-variable-specific
          condition-variable-specific-set! condition-variable-signal!
          condition-variable-broadcast!
          current-time time? time->seconds seconds->time
          current-exception-handler with-exception-handler raise
          join-timeout-exception? abandoned-mutex-exception?
          terminated-thread-exception? uncaught-exception?
          uncaught-exception-reason)
  (import (scheme base))
  (begin
    ;; ---- C primitive declarations (runtime/thread-sched.c,
    ;;      runtime/thread-lifecycle.c) ----
    (define-c-primitive (current-thread) any "npl_thread_current")
    (define-c-primitive (thread? (value any)) any "npl_thread_p")
    (define-c-primitive (thread-name (value any)) any "npl_thread_name")
    (define-c-primitive (thread-specific (value any)) any
      "npl_thread_specific")
    (define-c-primitive (thread-specific-set! (value any) (object any)) any
      "npl_thread_specific_set")
    (define-c-primitive (thread-start! (value any)) any "npl_thread_start")
    (define-c-primitive (thread-yield!) any "npl_thread_yield")
    (define-c-primitive (thread-sleep! (timeout any)) any
      "npl_thread_sleep")
    (define-c-primitive (thread-terminate! (value any)) any
      "npl_thread_terminate")

    (define-c-primitive (mutex? (value any)) any "npl_mutex_p")
    (define-c-primitive (mutex-name (value any)) any "npl_mutex_name")
    (define-c-primitive (mutex-specific (value any)) any
      "npl_mutex_specific")
    (define-c-primitive (mutex-specific-set! (value any) (object any)) any
      "npl_mutex_specific_set")
    (define-c-primitive (mutex-state (value any)) any "npl_mutex_state")

    (define-c-primitive (condition-variable? (value any)) any
      "npl_condvar_p")
    (define-c-primitive (condition-variable-name (value any)) any
      "npl_condvar_name")
    (define-c-primitive (condition-variable-specific (value any)) any
      "npl_condvar_specific")
    (define-c-primitive (condition-variable-specific-set!
                          (value any) (object any)) any
      "npl_condvar_specific_set")
    (define-c-primitive (condition-variable-signal! (value any)) any
      "npl_condvar_signal")
    (define-c-primitive (condition-variable-broadcast! (value any)) any
      "npl_condvar_broadcast")

    (define-c-primitive (current-time) any "npl_current_time")
    (define-c-primitive (time? (value any)) any "npl_time_p")
    (define-c-primitive (time->seconds (value any)) any
      "npl_time_seconds")
    (define-c-primitive (seconds->time (value any)) any
      "npl_seconds_time")
    (define-c-primitive (current-exception-handler) any
      "npl_current_exception_handler")

    (define-c-primitive (join-timeout-exception? (value any)) any
      "npl_join_timeout_exception_p")
    (define-c-primitive (abandoned-mutex-exception? (value any)) any
      "npl_abandoned_mutex_exception_p")
    (define-c-primitive (terminated-thread-exception? (value any)) any
      "npl_terminated_thread_exception_p")
    (define-c-primitive (uncaught-exception? (value any)) any
      "npl_uncaught_exception_p")
    (define-c-primitive (uncaught-exception-reason (value any)) any
      "npl_uncaught_exception_reason")

    ;; ---- private primitives with fixed arity; the optional-argument
    ;;      surface is NPL policy, not C. ----
    (define-c-primitive (%thread-make (thunk any) (name any)) any
      "npl_thread_make_prim")
    (define-c-primitive (%thread-join (thread any) (timeout any)
                          (timeout-val any) (count any)) any
      "npl_thread_join")
    (define-c-primitive (%mutex-make (name any)) any "npl_mutex_make_prim")
    (define-c-primitive (%condition-variable-make (name any)) any
      "npl_condvar_make_prim")
    (define-c-primitive (%mutex-lock (mutex any) (timeout any)
                          (thread any) (count any)) any "npl_mutex_lock")
    (define-c-primitive (%mutex-unlock (mutex any) (condition any)
                          (timeout any) (count any)) any
      "npl_mutex_unlock")

    ;; ---- public surface with optional arguments ----
    (define (make-thread thunk . maybe-name)
      (%thread-make thunk (if (null? maybe-name) #f (car maybe-name))))

    (define (make-mutex . maybe-name)
      (%mutex-make (if (null? maybe-name) #f (car maybe-name))))

    (define (make-condition-variable . maybe-name)
      (%condition-variable-make
        (if (null? maybe-name) #f (car maybe-name))))

    (define (thread-join! thread . rest)
      (let ((count (length rest)))
        (%thread-join thread
                      (if (>= count 1) (car rest) #f)
                      (if (>= count 2) (cadr rest) #f)
                      count)))

    (define (mutex-lock! mutex . rest)
      (let ((count (length rest)))
        (%mutex-lock mutex
                     (if (>= count 1) (car rest) #f)
                     (if (>= count 2) (cadr rest) #f)
                     count)))

    (define (mutex-unlock! mutex . rest)
      (let ((count (length rest)))
        (%mutex-unlock mutex
                       (if (>= count 1) (car rest) #f)
                       (if (>= count 2) (cadr rest) #f)
                       count)))))
