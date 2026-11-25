;; Shared nplc / npli behavior for (npl thread). Output must be identical on
;; both surfaces; keep every report deterministic (no scheduler races).
(define (report name value) (write (list name value)) (newline))

(report 'current-thread (thread? (current-thread)))

(let ((t (make-thread (lambda () 42) 'worker)))
  (report 'make-thread (thread-name t))
  (report 'thread-start (thread? (thread-start! t)))
  (report 'thread-join (thread-join! t))
  (thread-specific-set! t 'done)
  (report 'thread-specific (thread-specific t)))

(report 'thread-yield (begin (thread-yield!) #t))
(report 'thread-sleep (begin (thread-sleep! 0) #t))

(report 'current-time (time? (current-time)))
(report 'time->seconds (real? (time->seconds (current-time))))
(report 'seconds->time (time? (seconds->time 1.5)))

(let ((m (make-mutex 'gate)))
  (report 'mutex? (mutex? m))
  (report 'mutex-name (mutex-name m))
  (report 'mutex-state (mutex-state m))
  (mutex-specific-set! m 'specific)
  (report 'mutex-specific (mutex-specific m))
  (report 'mutex-lock! (mutex-lock! m))
  (report 'mutex-owned (thread? (mutex-state m)))
  (report 'mutex-unlock! (mutex-unlock! m))
  (report 'mutex-restored (eq? (mutex-state m) 'not-abandoned)))

(let ((cv (make-condition-variable 'cv)))
  (report 'condition-variable? (condition-variable? cv))
  (report 'condition-variable-name (condition-variable-name cv))
  (condition-variable-specific-set! cv 'specific)
  (report 'condition-variable-specific (condition-variable-specific cv))
  (condition-variable-signal! cv)
  (condition-variable-broadcast! cv)
  (report 'condition-variable-signal! #t))

(report 'join-timeout-exception? (not (join-timeout-exception? 1)))
(report 'abandoned-mutex-exception? (not (abandoned-mutex-exception? 1)))
(report 'terminated-thread-exception? (not (terminated-thread-exception? 1)))
(report 'uncaught-exception? (not (uncaught-exception? 1)))

(report 'current-exception-handler
       (begin (current-exception-handler) #t))
(report 'with-exception-handler
       (eq? (guard (e (#t e)) (raise 'raised) #f) 'raised))
