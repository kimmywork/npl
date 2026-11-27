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
;; call-in-continuation/call-in/return-to.  Composable call-in still needs a
;; terminal splice, so it is not supported yet.  See docs/control.md and
;; .agents/design/threads.md for the remaining T4-A/C boundary.
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
    (define-record-type prompt-tag-record
      (make-prompt-tag name)
      prompt-tag?
      (name prompt-tag-name))

    (define default-prompt-tag
      (make-prompt-tag 'default))

    (define barrier-prompt-tag
      (make-prompt-tag 'continuation-barrier))

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
    (define-c-primitive (%prompt-origin (prompt any)) any
      "npl_prompt_origin")
    (define-c-primitive (%prompt-set-barrier! (prompt any) (flag any)) any
      "npl_prompt_set_barrier")
    (define-c-primitive (%prompt-barrier? (prompt any)) any
      "npl_prompt_barrier_p")
    (define-c-primitive (%graft (procedure any) (arguments any)) any
      "npl_graft_make")

    (define-record-type continuation-record
      (make-continuation-record procedure kind origin tag prompts graft)
      continuation-record?
      (procedure continuation-record-procedure)
      (kind continuation-record-kind)
      (origin continuation-record-origin)
      (tag continuation-record-tag)
      (prompts continuation-record-prompts)
      (graft continuation-record-graft))

    (define continuations '())

    (define (raise-continuation-violation tag)
      (raise (make-continuation-violation tag)))

    (define (default-handler tag)
      (lambda (thunk)
        (if (procedure? thunk)
            (call-with-continuation-prompt thunk tag
                                           (default-handler tag))
            (error "default prompt handler expected a thunk" thunk))))

    (define (register-continuation! procedure kind origin tag prompts graft)
      (set! continuations
            (cons (make-continuation-record procedure kind origin tag
                                            prompts graft)
                  continuations))
      procedure)

    (define (continuation-info object)
      (let loop ((rest continuations))
        (if (null? rest)
            #f
            (if (eq? (continuation-record-procedure (car rest)) object)
                (car rest)
                (loop (cdr rest))))))

    (define (continuation? object)
      (if (continuation-info object) #t #f))

    (define (non-composable-continuation? object)
      (let ((info (continuation-info object)))
        (if (if info
                (eq? (continuation-record-kind info) 'non-composable)
                #f)
            #t
            #f)))

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

    (define (check-barrier! info)
      (if (barrier-crossed? (continuation-record-prompts info)
                            (%prompt-list-current))
          (raise-continuation-violation (continuation-record-tag info))
          #f))

    (define (make-continuation-graft continuation origin tag)
      (lambda (procedure arguments)
        (if origin
            (let ((cut (%prompt-find tag)))
              (if cut
                  (begin
                    (%prompt-install-override! origin (%prompt-escape cut))
                    (continuation (%graft procedure arguments)))
                  (raise-continuation-violation tag)))
            (continuation (%graft procedure arguments)))))

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
            ((%prompt-escape prompt) (make-abort-packet objects))
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
                                     (%prompt-install-override! origin return-k)
                                     (apply continuation args)))))
                             (registered
                               (register-continuation! wrapped 'composable
                                                       origin tag prompts #f)))
                        (proc wrapped)))))))))

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
                                 (begin
                                   (%prompt-install-override!
                                     origin (%prompt-escape cut))
                                   (apply continuation args))
                                 (raise-continuation-violation tag))))))
                    (register-continuation! wrapped 'non-composable origin tag
                                            prompts
                                            (make-continuation-graft
                                              continuation origin tag))
                    (proc wrapped))))))))

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
              (proc wrapped))))))

    (define call/cc call-with-current-continuation)

    (define (call-in-continuation continuation procedure . objects)
      (let ((info (continuation-info continuation)))
        (if (not info)
            (error "call-in-continuation: not a continuation"
                   continuation))
        (if (eq? (continuation-record-kind info) 'composable)
            (error "call-in-continuation: composable continuation is not supported yet"
                   continuation)
            (begin
              (check-barrier! info)
              (let ((graft (continuation-record-graft info)))
                (if graft
                    (graft procedure objects)
                    (error "call-in-continuation: no graft entry"
                           continuation)))))))

    (define (call-in continuation procedure . objects)
      (let ((info (continuation-info continuation)))
        (if (not info)
            (error "call-in: not a continuation" continuation))
        (if (eq? (continuation-record-kind info) 'composable)
            (error "call-in: expected a non-composable continuation"
                   continuation)
            (apply call-in-continuation continuation procedure objects))))

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
          (let* ((info (continuation-info (car maybe-continuation)))
                 (kind (if info (continuation-record-kind info) #f))
                 (captured-tag (if info (continuation-record-tag info) #f))
                 (stack (if info (continuation-record-prompts info)
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
