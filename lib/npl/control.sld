;; (npl control) — SRFI 226 control-system prototype (T4-A/T4-B subset).
;;
;; This library establishes the prompt/abort boundary and the two continuation
;; kinds on top of NPL's full call/cc:
;;   - non-composable invocation cuts the current continuation back to the
;;     nearest matching prompt and reinstates the captured prefix there;
;;   - composable invocation appends the captured prefix to the current
;;     continuation and returns its values to the caller.
;;
;; The captured prefix is re-entered on a helper native thread, because the
;; existing CPS continuation is opaque: it cannot be cut or spliced without a
;; compiler-visible segment representation.  That is the T4-A prototype
;; boundary, not the final architecture.  The helper restores the captured
;; dynamic environment through the runtime's cross-thread call/cc support and
;; redirects the origin prompt's terminal to a result thunk.
;;
;; Current limits: composable invocation from a virtual thread blocks its
;; carrier; concurrent invocation of one continuation and continuation barriers
;; are not supported; call-in-continuation/call-in/return-to are not implemented
;; yet.  See docs/control.md and .agents/design/threads.md.
(define-library (npl control)
  (export &continuation
          make-continuation-violation continuation-violation?
          continuation-violation-prompt-tag
          make-continuation-prompt-tag default-continuation-prompt-tag
          continuation-prompt-tag?
          call-with-continuation-prompt abort-current-continuation
          call-with-non-composable-continuation
          call-with-current-continuation call/cc
          call-with-composable-continuation
          continuation-prompt-available?
          continuation? non-composable-continuation?
          dynamic-wind unwind-protect
          reset reset-at shift shift-at)
  (import (except (scheme base)
                  call/cc call-with-current-continuation)
          (rename (only (scheme base)
                        call/cc call-with-current-continuation)
                  (call/cc core-call/cc)
                  (call-with-current-continuation core-call/cc2))
          (npl thread))
  (begin
    (define-record-type prompt-tag-record
      (make-prompt-tag name)
      prompt-tag?
      (name prompt-tag-name))

    (define default-prompt-tag
      (make-prompt-tag 'default))

    (define (default-continuation-prompt-tag)
      default-prompt-tag)

    (define (make-continuation-prompt-tag . maybe-name)
      (make-prompt-tag
        (if (null? maybe-name) (if #f #f) (car maybe-name))))

    (define (continuation-prompt-tag? object)
      (prompt-tag? object))

    (define-record-type prompt
      (make-prompt tag handler escape return lock)
      prompt?
      (tag prompt-tag)
      (handler prompt-handler)
      (escape prompt-escape set-prompt-escape!)
      (return prompt-return set-prompt-return!)
      (lock prompt-lock))

    (define-record-type abort-packet
      (make-abort-packet arguments)
      abort-packet?
      (arguments abort-packet-arguments))

    (define-record-type transfer-packet
      (make-transfer-packet thunk)
      transfer-packet?
      (thunk transfer-packet-thunk))

    (define-record-type continuation-violation
      (make-continuation-violation tag)
      continuation-violation?
      (tag continuation-violation-prompt-tag))

    ;; R7RS has no R6RS condition-type values.  Keep a stable feature symbol
    ;; for cond-expand/printing; the record predicate carries the behavior.
    (define &continuation '&continuation)

    (define prompt-stack (make-parameter '()))
    (define continuations '())

    (define (find-prompt tag stack)
      (cond
        ((null? stack) #f)
        ((eq? (prompt-tag (car stack)) tag) (car stack))
        (else (find-prompt tag (cdr stack)))))

    (define (raise-continuation-violation tag)
      (raise (make-continuation-violation tag)))

    (define (default-handler tag)
      (lambda (thunk)
        (if (procedure? thunk)
            (call-with-continuation-prompt thunk tag
                                           (default-handler tag))
            (error "default prompt handler expected a thunk" thunk))))

    (define (prompt-prefix origin stack)
      (cond
        ((null? stack) '())
        ((eq? (car stack) origin) (list origin))
        (else (cons (car stack) (prompt-prefix origin (cdr stack))))))

    (define (register-continuation! procedure kind origin tag stack)
      (set! continuations
            (cons (list procedure kind origin tag stack) continuations))
      procedure)

    (define (continuation-info object)
      (let loop ((rest continuations))
        (if (null? rest)
            #f
            (if (eq? (car (car rest)) object)
                (car rest)
                (loop (cdr rest))))))

    (define (continuation? object)
      (if (continuation-info object) #t #f))

    (define (non-composable-continuation? object)
      (let ((info (continuation-info object)))
        (if (if info (eq? (car (cdr info)) 'non-composable) #f) #t #f)))

    (define (call-with-continuation-prompt thunk . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (supplied-handler
               (if (if (pair? rest) (pair? (cdr rest)) #f)
                   (car (cdr rest))
                   #f))
             (handler (if supplied-handler
                          supplied-handler
                          (default-handler tag)))
             (prompt (make-prompt tag handler #f #f (make-mutex))))
        (call-with-values
          (lambda ()
            (core-call/cc
              (lambda (escape)
                (set-prompt-escape! prompt escape)
                (set-prompt-return! prompt
                                    (lambda vals (apply escape vals)))
                (parameterize ((prompt-stack
                                 (cons prompt (prompt-stack))))
                  (call-with-values
                    (lambda () (thunk))
                    (lambda vals
                      (apply (prompt-return prompt) vals)))))))
          (lambda results
            (cond
              ((if (= (length results) 1)
                   (abort-packet? (car results))
                   #f)
               (apply (prompt-handler prompt)
                      (abort-packet-arguments (car results))))
              ((if (= (length results) 1)
                   (transfer-packet? (car results))
                   #f)
               (let ((result-thunk
                      ((transfer-packet-thunk (car results)))))
                 (result-thunk)))
              (else (apply values results)))))))

    (define (abort-current-continuation tag . objects)
      (let ((prompt (find-prompt tag (prompt-stack))))
        (if prompt
            ((prompt-escape prompt) (make-abort-packet objects))
            (raise-continuation-violation tag))))

    ;; Return a thunk that delivers the captured prefix's values.  Redirect
    ;; the origin prompt's terminal to the helper, so the prefix returns here
    ;; instead of to the prompt's original caller.
    (define (invoke-captured origin continuation arguments)
      ;; Distinct continuations captured under one prompt share its return
      ;; slot.  Serialize invocations for that origin; the documented limit is
      ;; that a captured prefix must not recursively invoke the same origin.
      (mutex-lock! (prompt-lock origin))
      (dynamic-wind
        (lambda () (values))
        (lambda ()
          (let ((saved (prompt-return origin)))
            (let ((helper
                   (make-thread
                     (lambda ()
                       (core-call/cc
                         (lambda (exit)
                           (set-prompt-return!
                             origin
                             (lambda vals
                               (exit (lambda () (apply values vals)))))
                           (apply continuation arguments)))))))
              (thread-start! helper)
              (dynamic-wind
                (lambda () (values))
                (lambda () (thread-join! helper))
                (lambda () (set-prompt-return! origin saved))))))
        (lambda () (mutex-unlock! (prompt-lock origin)))))

    (define (call-with-composable-continuation proc . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (origin (find-prompt tag (prompt-stack))))
        (if (not origin)
            (raise-continuation-violation tag)
            (core-call/cc
              (lambda (continuation)
                (let ((wrapped
                       (lambda args
                         (let ((result
                                (invoke-captured origin continuation args)))
                           (result)))))
                  (register-continuation! wrapped 'composable origin tag
                                          (prompt-prefix origin
                                                         (prompt-stack)))
                  (proc wrapped)))))))

    (define (call-with-non-composable-continuation proc . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (origin (find-prompt tag (prompt-stack))))
        (if (not origin)
            (raise-continuation-violation tag)
            (core-call/cc
              (lambda (continuation)
                (let ((wrapped
                       (lambda args
                         (let ((cut (find-prompt tag (prompt-stack))))
                           (if cut
                               ((prompt-escape cut)
                                (make-transfer-packet
                                  (lambda ()
                                    (invoke-captured origin continuation
                                                     args))))
                               (raise-continuation-violation tag))))))
                  (register-continuation! wrapped 'non-composable origin tag
                                          (prompt-prefix origin
                                                         (prompt-stack)))
                  (proc wrapped)))))))

    (define (call-with-current-continuation proc)
      (core-call/cc
        (lambda (continuation)
          (let ((wrapped (lambda args (apply continuation args))))
            (register-continuation! wrapped 'non-composable #f
                                    default-prompt-tag
                                    (prompt-stack))
            (proc wrapped)))))

    (define call/cc call-with-current-continuation)

    (define (continuation-prompt-available? tag . maybe-continuation)
      (if (pair? maybe-continuation)
          (let* ((info (continuation-info (car maybe-continuation)))
                 (kind (if info (car (cdr info)) #f))
                 (captured-tag (if info (car (cdr (cdr (cdr info)))) #f))
                 (stack (if info (car (cdr (cdr (cdr (cdr info)))))
                            '())))
            (cond
              ((not info) #f)
              ((if (eq? kind 'composable) (eq? tag captured-tag) #f) #f)
              ((find-prompt tag stack) #t)
              (else #f)))
          (if (find-prompt tag (prompt-stack)) #t #f)))

    (define-syntax unwind-protect
      (syntax-rules ()
        ((_ protected cleanup ...)
         (dynamic-wind
           (lambda () (values))
           (lambda () protected)
           (lambda () cleanup ...)))))

    (define-syntax reset
      (syntax-rules ()
        ((_ expression ...)
         (call-with-continuation-prompt
           (lambda () expression ...)))))

    (define-syntax reset-at
      (syntax-rules ()
        ((_ tag expression ...)
         (let ((prompt-tag tag))
           (call-with-continuation-prompt
             (lambda () expression ...)
             prompt-tag)))))

    (define-syntax shift
      (syntax-rules ()
        ((_ identifier expression ...)
         (call-with-composable-continuation
           (lambda (continuation)
             (let ((identifier
                    (lambda args
                      (reset (apply continuation args)))))
               (abort-current-continuation
                 default-prompt-tag
                 (lambda () expression ...))))))))

    (define-syntax shift-at
      (syntax-rules ()
        ((_ tag identifier expression ...)
         (let ((prompt-tag tag))
           (call-with-composable-continuation
             (lambda (continuation)
               (let ((identifier
                      (lambda args
                        (reset-at prompt-tag
                                  (apply continuation args)))))
                 (abort-current-continuation
                   prompt-tag
                   (lambda () expression ...))))
             prompt-tag))))))
)
