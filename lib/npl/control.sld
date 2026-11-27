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
                  (call-with-current-continuation core-call/cc2)))
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

    (define-c-primitive (%prompt-make (tag any) (handler any)) any
      "npl_prompt_make")
    (define-c-primitive (%prompt? (value any)) any "npl_prompt_p")
    (define-c-primitive (%prompt-tag (value any)) any "npl_prompt_tag")
    (define-c-primitive (%prompt-handler (value any)) any
      "npl_prompt_handler")
    (define-c-primitive (%prompt-escape (value any)) any "npl_prompt_escape")
    (define-c-primitive (%prompt-set-escape! (value any) (escape any)) any
      "npl_prompt_set_escape")
    (define-c-primitive (%prompt-return (value any)) any "npl_prompt_return")
    (define-c-primitive (%prompt-set-return! (value any) (return any)) any
      "npl_prompt_set_return")
    (define-c-primitive (%prompt-push! (value any)) any "npl_prompt_push")
    (define-c-primitive (%prompt-pop! (value any)) any "npl_prompt_pop")
    (define-c-primitive (%prompt-find (tag any)) any "npl_prompt_find")
    (define-c-primitive (%prompt-list-current) any "npl_prompt_list_current")
    (define-c-primitive (%prompt-list-from (origin any)) any
      "npl_prompt_list_from")
    (define-c-primitive (%prompt-install-override! (prompt any) (target any))
      any "npl_prompt_install_override")
    (define-c-primitive (%prompt-take-return! (prompt any)) any
      "npl_prompt_take_return")

    (define continuations '())

    (define (raise-continuation-violation tag)
      (raise (make-continuation-violation tag)))

    (define (default-handler tag)
      (lambda (thunk)
        (if (procedure? thunk)
            (call-with-continuation-prompt thunk tag
                                           (default-handler tag))
            (error "default prompt handler expected a thunk" thunk))))

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

    (define (prompt-finish prompt values)
      (let ((target (%prompt-take-return! prompt)))
        (apply (if target target (%prompt-return prompt)) values)))

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
             (prompt (%prompt-make tag handler)))
        (call-with-values
          (lambda ()
            (core-call/cc
              (lambda (escape)
                (%prompt-set-escape! prompt escape)
                (%prompt-set-return! prompt
                                     (lambda vals (apply escape vals)))
                (%prompt-push! prompt)
                (call-with-values
                  (lambda () (thunk))
                  (lambda vals
                    (%prompt-pop! prompt)
                    (prompt-finish prompt vals))))))
          (lambda results
            (cond
              ((if (= (length results) 1)
                   (abort-packet? (car results))
                   #f)
               (apply (%prompt-handler prompt)
                      (abort-packet-arguments (car results))))
              ((if (= (length results) 1)
                   (transfer-packet? (car results))
                   #f)
               (let ((result-thunk
                      ((transfer-packet-thunk (car results)))))
                 (result-thunk)))
              (else (apply values results)))))))

    (define (abort-current-continuation tag . objects)
      (let ((prompt (%prompt-find tag)))
        (if prompt
            ((%prompt-escape prompt) (make-abort-packet objects))
            (raise-continuation-violation tag))))

    (define (call-with-composable-continuation proc . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (origin (%prompt-find tag)))
        (if (not origin)
            (raise-continuation-violation tag)
            (core-call/cc
              (lambda (continuation)
                (let ((wrapped
                       (lambda args
                         (core-call/cc
                           (lambda (return-k)
                             (%prompt-install-override! origin return-k)
                             (apply continuation args))))))
                  (register-continuation! wrapped 'composable origin tag
                                          (%prompt-list-from origin))
                  (proc wrapped)))))))

    (define (call-with-non-composable-continuation proc . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (origin (%prompt-find tag)))
        (if (not origin)
            (raise-continuation-violation tag)
            (core-call/cc
              (lambda (continuation)
                (let ((wrapped
                       (lambda args
                         (let ((cut (%prompt-find tag)))
                           (if cut
                               (begin
                                 (%prompt-install-override!
                                   origin (%prompt-escape cut))
                                 (apply continuation args))
                               (raise-continuation-violation tag))))))
                  (register-continuation! wrapped 'non-composable origin tag
                                          (%prompt-list-from origin))
                  (proc wrapped)))))))

    (define (call-with-current-continuation proc)
      (core-call/cc
        (lambda (continuation)
          (let ((wrapped (lambda args (apply continuation args))))
            (register-continuation! wrapped 'non-composable #f
                                    default-prompt-tag
                                    (%prompt-list-current))
            (proc wrapped)))))

    (define call/cc call-with-current-continuation)

    (define (prompt-tag-in-list? tag prompts)
      (cond
        ((null? prompts) #f)
        ((eq? (%prompt-tag (car prompts)) tag) #t)
        (else (prompt-tag-in-list? tag (cdr prompts)))))

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
              ((prompt-tag-in-list? tag stack) #t)
              (else #f)))
          (if (%prompt-find tag) #t #f)))

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
