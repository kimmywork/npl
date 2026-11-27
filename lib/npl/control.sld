;; (npl control) — SRFI 226 control-system prototype (T4-A/T4-B subset).
;;
;; This library establishes the prompt/abort boundary and the two continuation
;; kinds on compiler-emitted continuation segments and runtime prompt frames:
;;   - non-composable invocation cuts the current continuation back to the
;;     nearest matching prompt and reinstates the captured prefix there;
;;   - composable invocation appends the captured prefix to the current
;;     continuation and returns its values to the caller.
;;
;; A prompt frame records its origin segment and a barrier flag in the shared
;; runtime dynamic context.  Capture stores the prompt prefix; barriers are
;; enforced by frame identity, and a graft packet implements
;; call-in-continuation/call-in/return-to.  A composable invocation splices
;; the captured prefix's delimiter into the invoking prompt chain and keeps
;; its own one-shot return relation, so multi-shot activations are
;; independent.  See docs/control.md and .agents/design/threads.md for the
;; remaining T4-C boundary.
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
          call-in-continuation call-in return-to
          call-with-continuation-barrier
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
    (define-c-primitive (%default-prompt-tag) any
      "npl_default_prompt_tag")

    (define-record-type prompt-tag-record
      (make-prompt-tag name)
      prompt-tag?
      (name prompt-tag-name))

    (define default-prompt-tag
      (%default-prompt-tag))

    (define barrier-prompt-tag
      (make-prompt-tag 'continuation-barrier))

    (define (default-continuation-prompt-tag)
      default-prompt-tag)

    (define (make-continuation-prompt-tag . maybe-name)
      (make-prompt-tag
        (if (null? maybe-name) (if #f #f) (car maybe-name))))

    (define (continuation-prompt-tag? object)
      (if (prompt-tag? object)
          #t
          (eq? object default-prompt-tag)))

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
    (define-c-primitive (%prompt-current) any "npl_prompt_current")
    (define-c-primitive (%prompt-install-splice (origin any) (tail any)) any
      "npl_prompt_install_splice")
    (define-c-primitive (%prompt-install-override! (prompt any) (target any))
      any "npl_prompt_install_override")
    (define-c-primitive (%prompt-take-return! (prompt any)) any
      "npl_prompt_take_return")
    (define-c-primitive (%prompt-origin (prompt any)) any
      "npl_prompt_origin")
    (define-c-primitive (%prompt-set-barrier! (prompt any) (flag any)) any
      "npl_prompt_set_barrier")
    (define-c-primitive (%prompt-barrier? (prompt any)) any
      "npl_prompt_barrier_p")
    (define-c-primitive (%graft (procedure any) (arguments any)
                                (prompt any) (target any) (splice any)) any
      "npl_graft_make")
    (define-c-primitive (%continuation-make (procedure any) (kind any)
                                            (origin any) (tag any)
                                            (prompts any) (graft any)) any
      "npl_continuation_make")
    (define-c-primitive (%continuation? (value any)) any
      "npl_continuation_p")
    (define-c-primitive (%continuation-kind (value any)) any
      "npl_continuation_kind")
    (define-c-primitive (%continuation-origin (value any)) any
      "npl_continuation_origin")
    (define-c-primitive (%continuation-tag (value any)) any
      "npl_continuation_tag")
    (define-c-primitive (%continuation-prompts (value any)) any
      "npl_continuation_prompts")
    (define-c-primitive (%continuation-graft (value any)) any
      "npl_continuation_graft")
    (define-c-primitive (%prompt-root? (value any)) any
      "npl_prompt_root_p")
    (define-c-primitive (%prompt-root-kind (value any)) any
      "npl_prompt_root_kind")
    (define-c-primitive (%root-abort (prompt any) (handler any)) any
      "npl_prompt_root_abort")

    (define (raise-continuation-violation tag)
      (raise (make-continuation-violation tag)))

    (define (default-handler tag)
      (lambda (thunk)
        (if (procedure? thunk)
            (call-with-continuation-prompt thunk tag
                                           (default-handler tag))
            (error "default prompt handler expected a thunk" thunk))))

    (define (register-continuation! procedure kind origin tag prompts graft)
      (%continuation-make procedure kind origin tag prompts graft))

    (define (continuation? object)
      (%continuation? object))

    (define (non-composable-continuation? object)
      (if (%continuation? object)
          (eq? (%continuation-kind object) 'non-composable)
          #f))

    (define (list-memq object values)
      (cond
        ((null? values) #f)
        ((eq? object (car values)) #t)
        (else (list-memq object (cdr values)))))

    ;; Prompt frames from the capture point out through origin (inclusive).
    (define (prompt-prefix origin prompts)
      (cond
        ((null? prompts) '())
        ((eq? (car prompts) origin) (list origin))
        (else (cons (car prompts)
                    (prompt-prefix origin (cdr prompts))))))

    (define (barrier-in-list? prompts)
      (cond
        ((null? prompts) #f)
        ((%prompt-barrier? (car prompts)) #t)
        (else (barrier-in-list? (cdr prompts)))))

    ;; Jumping into a barrier that is not active in the invoking continuation
    ;; is prohibited; escaping outward is not.
    (define (barrier-crossed? captured current)
      (cond
        ((null? captured) #f)
        ((if (%prompt-barrier? (car captured))
             (not (list-memq (car captured) current))
             #f)
         #t)
        (else (barrier-crossed? (cdr captured) current))))

    (define (check-barrier! continuation)
      (if (barrier-crossed? (%continuation-prompts continuation)
                            (%prompt-list-current))
          (raise-continuation-violation (%continuation-tag continuation))
          #f))

    ;; The packet carries the return relation into the target dynamic
    ;; environment.  The generated continuation installs the one-shot
    ;; override after its wind transfer, so a non-local exit before the
    ;; terminal cannot leave stale state in this thread's context.
    (define (make-continuation-graft continuation origin tag)
      (lambda (procedure arguments)
        (if origin
            (let ((cut (%prompt-find tag)))
              (if cut
                  (continuation
                    (%graft procedure arguments origin
                            (%prompt-escape cut) #f))
                  (raise-continuation-violation tag)))
            ;; A non-composable continuation has no invoking prompt suffix to
            ;; compose with the captured prefix.
            (continuation (%graft procedure arguments #f #f #f)))))

    ;; A composable call-in keeps the captured prefix's normal terminal: the
    ;; invocation's continuation is the composed result, so the prompt
    ;; terminal returns to this activation.  Each invocation gets its own
    ;; return-k; the captured continuation description is not mutated.
    (define (make-composable-graft continuation origin)
      (lambda (procedure arguments)
        (core-call/cc
          (lambda (return-k)
            ;; The invoking prompt chain is the outer half of the composed
            ;; continuation.  The runtime installs it after the captured
            ;; environment transfer, so availability and aborts see the
            ;; caller's prompts (SRFI 226 delimited, not captured, prompt).
            (continuation
              (%graft procedure arguments origin return-k
                      (%prompt-current)))))))

    (define (prompt-finish prompt values)
      (let ((target (%prompt-take-return! prompt)))
        (apply (if target target (%prompt-return prompt)) values)))

    (define (call-with-continuation-prompt-internal
              thunk tag handler barrier?)
      (let ((prompt (%prompt-make tag handler)))
        (if barrier? (%prompt-set-barrier! prompt #t) #f)
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

    (define (call-with-continuation-prompt thunk . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (supplied-handler
               (if (if (pair? rest) (pair? (cdr rest)) #f)
                   (car (cdr rest))
                   #f))
             (handler (if supplied-handler
                          supplied-handler
                          (default-handler tag))))
        (call-with-continuation-prompt-internal thunk tag handler #f)))

    (define (abort-current-continuation tag . objects)
      (let ((prompt (%prompt-find tag)))
        (if prompt
            (if (%prompt-root? prompt)
                (if (= (%prompt-root-kind prompt) 0)
                    ;; A legacy seed installed only the plain runtime root.
                    ;; It has no landing pad for a nested trampoline, so keep
                    ;; the old best-effort path; current generated entries use
                    ;; npl_program_run and the transfer-based branch below.
                    (call-with-values
                      (lambda () (apply (default-handler tag) objects))
                      (lambda results
                        (apply (%prompt-return prompt) results)))
                    ;; T4-B0: transfer to the captured root environment first,
                    ;; then run the handler there.  The root finisher unwinds
                    ;; to the program/thread/virtual-thread landing pad.
                    ((%root-abort prompt (default-handler tag)) objects))
                ((%prompt-escape prompt) (make-abort-packet objects)))
            (raise-continuation-violation tag))))

    (define (call-with-composable-continuation proc . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (origin (%prompt-find tag)))
        (if (not origin)
            (raise-continuation-violation tag)
            (let ((prompts (prompt-prefix origin (%prompt-list-current))))
              (if (barrier-in-list? prompts)
                  (raise-continuation-violation tag)
                  (core-call/cc
                    (lambda (continuation)
                      (let* ((wrapped
                               (lambda args
                                 (core-call/cc
                                   (lambda (return-k)
                                     (continuation
                                       (%graft '() args origin return-k
                                               (%prompt-current)))))))
                             (registered
                               (register-continuation! wrapped 'composable
                                                       origin tag prompts
                                                       (make-composable-graft
                                                         continuation
                                                         origin))))
                        (proc registered)))))))))

    (define (call-with-non-composable-continuation proc . rest)
      (let* ((tag (if (pair? rest) (car rest)
                      default-prompt-tag))
             (origin (%prompt-find tag)))
        (if (not origin)
            (raise-continuation-violation tag)
            (let ((prompts (prompt-prefix origin (%prompt-list-current))))
              (core-call/cc
                (lambda (continuation)
                  (let ((wrapped
                         (lambda args
                           (if (barrier-crossed? prompts
                                                 (%prompt-list-current))
                               (raise-continuation-violation tag)
                               #f)
                           (let ((cut (%prompt-find tag)))
                             (if cut
                                 (continuation
                                   (%graft '() args origin
                                           (%prompt-escape cut) #f))
                                 (raise-continuation-violation tag))))))
                    (let ((registered
                           (register-continuation! wrapped 'non-composable
                                                   origin tag prompts
                                                   (make-continuation-graft
                                                     continuation origin tag))))
                      (proc registered)))))))))

    (define (call-with-current-continuation proc)
      (let ((prompts (%prompt-list-current)))
        (core-call/cc
          (lambda (continuation)
            (let* ((wrapped
                     (lambda args
                       (if (barrier-crossed? prompts
                                             (%prompt-list-current))
                           (raise-continuation-violation
                             default-prompt-tag)
                           #f)
                       (apply continuation args)))
                   (registered
                     (register-continuation! wrapped 'non-composable #f
                                             default-prompt-tag prompts
                                             (make-continuation-graft
                                               continuation #f #f))))
              (proc registered))))))

    (define call/cc call-with-current-continuation)

    (define (call-in-continuation continuation procedure . objects)
      (if (not (%continuation? continuation))
          (error "call-in-continuation: not a continuation"
                 continuation))
      (if (not (procedure? procedure))
          (error "call-in-continuation: expected a procedure" procedure)
          (begin
            (check-barrier! continuation)
            (let ((graft (%continuation-graft continuation)))
              (if graft
                  (graft procedure objects)
                  (error "call-in-continuation: no graft entry"
                         continuation))))))

    (define (call-in continuation procedure . objects)
      (if (not (%continuation? continuation))
          (error "call-in: not a continuation" continuation))
      (if (eq? (%continuation-kind continuation) 'composable)
          (error "call-in: expected a non-composable continuation"
                 continuation)
          (apply call-in-continuation continuation procedure objects)))

    (define (return-to continuation . objects)
      (apply call-in continuation values objects))

    (define (barrier-handler . objects)
      (raise (make-continuation-violation barrier-prompt-tag)))

    (define (call-with-continuation-barrier thunk)
      (if (not (procedure? thunk))
          (error "call-with-continuation-barrier: expected a procedure"
                 thunk))
      (call-with-continuation-prompt-internal
        (lambda () (thunk))
        barrier-prompt-tag
        barrier-handler
        #t))

    (define (prompt-tag-in-list? tag prompts)
      (cond
        ((null? prompts) #f)
        ((eq? (%prompt-tag (car prompts)) tag) #t)
        (else (prompt-tag-in-list? tag (cdr prompts)))))

    (define (continuation-prompt-available? tag . maybe-continuation)
      (if (pair? maybe-continuation)
          (let ((continuation (car maybe-continuation)))
            (if (not (%continuation? continuation))
                #f
                (let ((kind (%continuation-kind continuation))
                      (captured-tag (%continuation-tag continuation))
                      (stack (%continuation-prompts continuation)))
                  (cond
                    ((if (eq? kind 'composable)
                         (eq? tag captured-tag)
                         #f)
                     #f)
                    ((prompt-tag-in-list? tag stack) #t)
                    (else #f)))))
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
