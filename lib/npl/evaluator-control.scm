; Evaluator dynamic control: dynamic-wind frames, continuations, parameter
; objects, promises, and exception handlers, implemented over the evaluator's
; own CPS (evaluator-core.scm).

(define (eval-wind-parent frame) (vector-ref frame 3))
(define (eval-handler-parent frame) (vector-ref frame 2))
(define (eval-parameter-binding-parent frame) (vector-ref frame 3))

(define (eval-wind-depth frame)
  (if (null? frame) 0 (+ 1 (eval-wind-depth (eval-wind-parent frame)))))

(define (eval-wind-common source target)
  (let ((source-depth (eval-wind-depth source))
        (target-depth (eval-wind-depth target)))
    (letrec ((align
              (lambda (left right left-depth right-depth)
                (if (> left-depth right-depth)
                    (align (eval-wind-parent left) right
                           (- left-depth 1) right-depth)
                    (if (> right-depth left-depth)
                        (align left (eval-wind-parent right)
                               left-depth (- right-depth 1))
                        (if (eq? left right)
                            left
                            (align (eval-wind-parent left)
                                   (eval-wind-parent right)
                                   (- left-depth 1) (- right-depth 1))))))))
      (align source target source-depth target-depth))))

(define (eval-wind-transfer target handler parameters continuation value)
  (letrec
    ((step
       (lambda (transition)
         (let ((exits (vector-ref transition 1))
               (entries (vector-ref transition 2)))
           (if (pair? exits)
               (let ((frame (car exits)))
                 (vector-set! transition 1 (cdr exits))
                 (set! EVAL-CURRENT-WIND (eval-wind-parent frame))
                 (eval-apply (vector-ref frame 2) '() '()
                   (lambda (ignored) (step transition))))
               (if (pair? entries)
                   (let ((frame (car entries)))
                     (vector-set! transition 2 (cdr entries))
                     (set! EVAL-CURRENT-HANDLER (vector-ref transition 4))
                     (eval-apply (vector-ref frame 1) '() '()
                       (lambda (ignored)
                         (set! EVAL-CURRENT-WIND frame)
                         (step transition))))
                   (begin
                     (set! EVAL-CURRENT-WIND (vector-ref transition 3))
                     (set! EVAL-CURRENT-HANDLER (vector-ref transition 4))
                     (set! EVAL-CURRENT-PARAMETERS (vector-ref transition 5))
                     ((vector-ref transition 6)
                      (vector-ref transition 7)))))))))
    (if (eq? EVAL-CURRENT-WIND target)
        (begin
          (set! EVAL-CURRENT-HANDLER handler)
          (set! EVAL-CURRENT-PARAMETERS parameters)
          (continuation value))
        (let ((common (eval-wind-common EVAL-CURRENT-WIND target)))
          (letrec ((collect
                    (lambda (frame)
                      (if (eq? frame common)
                          '()
                          (cons frame
                                (collect (eval-wind-parent frame)))))))
            (step
              (vector 911
                      (collect EVAL-CURRENT-WIND)
                      (reverse (collect target))
                      target handler parameters continuation value)))))))

(define (eval-restore-continuation captured value)
  (eval-wind-transfer
    (vector-ref captured 2)
    (vector-ref captured 3)
    (vector-ref captured 4)
    (vector-ref captured 1)
    value))

(define (eval-single value who continuation)
  (if (eval-error? value)
      (continuation value)
      (if (eval-values? value)
          (let ((values (vector-ref value 1)))
            (if (if (pair? values) (null? (cdr values)) #f)
                (continuation (car values))
                (continuation
                  (eval-error
                    (string-append who " needs one value")))))
          (continuation value))))

(define (eval-parameter-binding-find parameter bindings)
  (if (null? bindings)
      #f
      (if (eq? parameter (vector-ref bindings 1))
          bindings
          (eval-parameter-binding-find
            parameter (eval-parameter-binding-parent bindings)))))

(define (eval-parameter-ref parameter)
  (let ((binding
          (eval-parameter-binding-find parameter EVAL-CURRENT-PARAMETERS)))
    (if binding (vector-ref binding 2) (vector-ref parameter 2))))

(define (eval-parameter-store! parameter value)
  (let ((binding
          (eval-parameter-binding-find parameter EVAL-CURRENT-PARAMETERS)))
    (if binding
        (vector-set! binding 2 value)
        (vector-set! parameter 2 value))))

(define (eval-parameter-update parameter value continuation)
  (let ((converter (vector-ref parameter 1)))
    (if (null? converter)
        (begin
          (eval-parameter-store! parameter value)
          (continuation EVAL-UNSPECIFIED))
        (eval-apply converter (list value) '()
          (lambda (converted)
            (eval-single converted "parameter converter"
              (lambda (single)
                (if (eval-error? single)
                    (continuation single)
                    (begin
                      (eval-parameter-store! parameter single)
                      (continuation EVAL-UNSPECIFIED))))))))))

(define (eval-callable? value)
  (if (if (eval-tagged? value EVAL-PRIMITIVE-TAG) #t
          (if (eval-tagged? value EVAL-CLOSURE-TAG) #t
              (if (eval-tagged? value EVAL-CONTINUATION-TAG) #t
                  (if (eval-parameter? value) #t
                      (eval-promise? value)))))
      #t #f))

(define (eval-force-complete root value continuation)
  (if (= (eval-promise-state root) EVAL-PROMISE-DONE)
      (continuation (eval-promise-value root))
      (begin
        (eval-promise-set! root EVAL-PROMISE-DONE value)
        (continuation value))))

(define (eval-force-step root value environment continuation)
  (if (eval-promise? value)
      (let ((state (eval-promise-state value)))
        (cond
          ((= state EVAL-PROMISE-DONE)
           (eval-force-complete root (eval-promise-value value) continuation))
          ((= state EVAL-PROMISE-REDIRECT)
           (eval-force-step root (eval-promise-value value)
                            environment continuation))
          ((if (= state EVAL-PROMISE-DELAY) #t
               (= state EVAL-PROMISE-CHAIN))
           (eval-apply (eval-promise-value value) '() environment
             (lambda (result)
               (if (eval-error? result)
                   (continuation result)
                   (if (= (eval-promise-state value) EVAL-PROMISE-DELAY)
                       (begin
                         (eval-promise-set! value EVAL-PROMISE-DONE result)
                         (eval-force-step root value environment continuation))
                       (if (= (eval-promise-state value) EVAL-PROMISE-CHAIN)
                           (begin
                             (eval-promise-set! value EVAL-PROMISE-REDIRECT
                                                result)
                             (eval-force-step root result
                                              environment continuation))
                           (eval-force-step root value
                                            environment continuation)))))))
          (else (continuation (eval-error "force: invalid promise state")))))
      (eval-force-complete root value continuation)))

(define (eval-force promise environment continuation)
  (eval-force-step promise promise environment continuation))

(define (eval-promise-op arguments continuation)
  (let ((error (eval-arity arguments 2 "promise operation")))
    (if error
        (continuation error)
        (let ((operation (car arguments))
              (value (car (cdr arguments))))
          (if (not (number? operation))
              (continuation
                (eval-error "promise operation: expected two arguments"))
              (cond
                ((= operation 0)
                 (continuation
                   (if (eval-promise? value)
                       value
                       (vector EVAL-PROMISE-TAG EVAL-PROMISE-DONE value))))
                ((= operation 1) (continuation (eval-promise? value)))
                ((= operation 2)
                 (if (eval-tagged? value EVAL-CLOSURE-TAG)
                     (continuation
                       (vector EVAL-PROMISE-TAG EVAL-PROMISE-DELAY value))
                     (continuation (eval-error "delay: not a procedure"))))
                ((= operation 3)
                 (if (eval-tagged? value EVAL-CLOSURE-TAG)
                     (continuation
                       (vector EVAL-PROMISE-TAG EVAL-PROMISE-CHAIN value))
                     (continuation
                       (eval-error "delay-force: not a procedure"))))
                (else
                 (continuation
                   (eval-error "promise operation: invalid operation")))))))))

(define (eval-install-wind before thunk after environment continuation)
  (let ((frame (vector EVAL-WIND-TAG before after EVAL-CURRENT-WIND)))
    (eval-apply before '() environment
      (lambda (before-value)
        (if (eval-error? before-value)
            (continuation before-value)
            (begin
              (set! EVAL-CURRENT-WIND frame)
              (eval-apply thunk '() environment
                (lambda (body-value)
                  (set! EVAL-CURRENT-WIND (eval-wind-parent frame))
                  (eval-apply after '() environment
                    (lambda (after-value)
                      (if (eval-error? after-value)
                          (continuation after-value)
                          (continuation body-value))))))))))))

(define (eval-dynamic-wind arguments environment continuation)
  (if (not (= (length arguments) 3))
      (continuation
        (eval-error "dynamic-wind: wrong number of arguments"))
      (if (if (eval-callable? (car arguments))
              (if (eval-callable? (car (cdr arguments)))
                  (eval-callable? (car (cdr (cdr arguments))))
                  #f)
              #f)
          (eval-install-wind (car arguments) (car (cdr arguments))
                             (car (cdr (cdr arguments)))
                             environment continuation)
          (continuation
            (eval-error "dynamic-wind: arguments must be procedures")))))

(define (eval-handler-push handler)
  (let ((frame (vector EVAL-HANDLER-TAG handler EVAL-CURRENT-HANDLER)))
    (set! EVAL-CURRENT-HANDLER frame)
    frame))

; Primitive errors (docs/plan-r7rs.md "Primitive Errors"): the same error
; object layout as native (runtime/error.c, lib/scheme/error.scm), raised
; non-continuably; uncaught, it becomes an evaluator error with the text
; "who: message: irritant". EVAL-PRIMITIVE-ENVIRONMENT is in core.

(define (eval-primitive-error who message irritant continuation)
  (eval-raise-value
    (npl_make_record 'npl-error-object 'error
                     (string-append who (string-append ": " message))
                     (list irritant))
    #f EVAL-PRIMITIVE-ENVIRONMENT continuation))

(define (eval-error-object-text object)
  (let ((port (open-output-string)))
    (display (npl_record_ref object 'npl-error-object 2) port)
    (for-each (lambda (irritant) (display ": " port) (write irritant port))
              (npl_record_ref object 'npl-error-object 3))
    (get-output-string port)))

(define (eval-raise-value value continuable environment continuation)
  (if (null? EVAL-CURRENT-HANDLER)
      (continuation
        (eval-error
          (if (npl_record_p value 'npl-error-object)
              (eval-error-object-text value)
              "uncaught exception")))
      (let ((frame EVAL-CURRENT-HANDLER))
        (set! EVAL-CURRENT-HANDLER (eval-handler-parent frame))
        (eval-apply (vector-ref frame 1) (list value) environment
          (lambda (handled)
            (if continuable
                (begin
                  (set! EVAL-CURRENT-HANDLER frame)
                  (continuation handled))
                (eval-raise-value value #f environment continuation)))))))

(define (eval-with-handler handler thunk environment continuation)
  (let ((frame (eval-handler-push handler)))
    (eval-apply thunk '() environment
      (lambda (value)
        (if (eq? EVAL-CURRENT-HANDLER frame)
            (set! EVAL-CURRENT-HANDLER (eval-handler-parent frame))
            #f)
        (continuation value)))))

(define (eval-parameter-enter parameters values frames)
  (letrec ((loop
            (lambda (index)
              (if (= index (vector-length parameters))
                  EVAL-UNSPECIFIED
                  (let ((frame-value (vector-ref frames index)))
                    (if (eq? frame-value #f)
                        (let ((created
                                (vector EVAL-PARAMETER-BINDING-TAG
                                        (vector-ref parameters index)
                                        (vector-ref values index)
                                        EVAL-CURRENT-PARAMETERS)))
                          (vector-set! frames index created)
                          (set! EVAL-CURRENT-PARAMETERS created))
                        (set! EVAL-CURRENT-PARAMETERS frame-value))
                    (loop (+ index 1)))))))
    (loop 0)))

(define (eval-parameter-leave frames)
  (letrec ((loop
            (lambda (index)
              (if (< index 0)
                  EVAL-UNSPECIFIED
                  (let ((frame (vector-ref frames index)))
                    (if (if (eval-parameter-binding? frame)
                            (eq? EVAL-CURRENT-PARAMETERS frame)
                            #f)
                        (begin
                          (set! EVAL-CURRENT-PARAMETERS
                                (eval-parameter-binding-parent frame))
                          (loop (- index 1)))
                        (eval-error "parameterize: non-current binding exit")))))))
    (loop (- (vector-length frames) 1))))
