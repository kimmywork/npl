; Shared persistent evaluator used by the REPL and the future (scheme eval).

(define EVAL-PRIMITIVE-TAG 901)
(define EVAL-CLOSURE-TAG 902)
(define EVAL-ERROR-TAG 903)
(define EVAL-VALUES-TAG 904)
(define EVAL-CONTINUATION-TAG 905)
(define EVAL-PARAMETER-TAG 907)
(define EVAL-WIND-TAG 908)
(define EVAL-HANDLER-TAG 909)
(define EVAL-PARAMETER-BINDING-TAG 910)
(define EVAL-UNSPECIFIED (vector 906))

(define EVAL-CURRENT-WIND '())
(define EVAL-CURRENT-HANDLER '())
(define EVAL-CURRENT-PARAMETERS '())

(define (eval-tagged? value tag)
  (if (vector? value)
      (if (> (vector-length value) 0)
          (= (vector-ref value 0) tag)
          #f)
      #f))

(define (eval-error message) (vector EVAL-ERROR-TAG message))
(define (eval-error? value) (eval-tagged? value EVAL-ERROR-TAG))
(define (eval-error-message value) (vector-ref value 1))
(define (eval-unspecified? value) (eq? value EVAL-UNSPECIFIED))
(define (eval-values values) (vector EVAL-VALUES-TAG values))
(define (eval-values? value) (eval-tagged? value EVAL-VALUES-TAG))
(define (eval-values-list value) (vector-ref value 1))
(define (eval-continuation continuation)
  (vector EVAL-CONTINUATION-TAG continuation
          EVAL-CURRENT-WIND EVAL-CURRENT-HANDLER
          EVAL-CURRENT-PARAMETERS))

(define (eval-parameter? value) (eval-tagged? value EVAL-PARAMETER-TAG))
(define (eval-parameter-binding? value)
  (eval-tagged? value EVAL-PARAMETER-BINDING-TAG))

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
                  (eval-parameter? value))))
      #t #f))

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

(define (eval-raise-value value continuable environment continuation)
  (if (null? EVAL-CURRENT-HANDLER)
      (continuation (eval-error "uncaught exception"))
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

(define (eval-core-tagged? value tag)
  (if (pair? value)
      (if (number? (car value)) (= (car value) tag) #f)
      #f))

(define (eval-core-symbol? value) (eval-core-tagged? value 100))

(define (eval-name value)
  (if (eval-core-symbol? value)
      (cdr value)
      (if (symbol? value) (symbol->string value) "")))

(define (eval-named? value name)
  (string=? (eval-name value) name))

(define (eval-proper-list? value)
  (if (null? value)
      #t
      (if (pair? value) (eval-proper-list? (cdr value)) #f)))

; A frame is a pair whose car is a mutable association list.
(define (eval-frame) (cons '() '()))
(define (eval-environment frame parent) (cons frame parent))

(define (eval-frame-find name bindings)
  (if (null? bindings)
      #f
      (if (string=? name (car (car bindings)))
          (car bindings)
          (eval-frame-find name (cdr bindings)))))

(define (eval-find-binding name environment)
  (if (null? environment)
      #f
      (let ((binding (eval-frame-find name (car (car environment)))))
        (if binding binding
            (eval-find-binding name (cdr environment))))))

(define (eval-define! environment name value)
  (let ((frame (car environment)))
    (let ((binding (eval-frame-find name (car frame))))
      (if binding
          (set-car! (cdr binding) value)
          (set-car! frame
                    (cons (cons name (cons value '())) (car frame))))))
  EVAL-UNSPECIFIED)

(define (eval-lookup environment identifier)
  (let ((name (eval-name identifier)))
    (let ((binding (eval-find-binding name environment)))
      (if binding
          (car (cdr binding))
          (eval-error (string-append "unbound identifier: " name))))))

(define (eval-set! environment identifier value)
  (let ((name (eval-name identifier)))
    (let ((binding (eval-find-binding name environment)))
      (if binding
          (begin (set-car! (cdr binding) value) EVAL-UNSPECIFIED)
          (eval-error (string-append "unbound identifier: " name))))))

(define (eval-primitive name) (vector EVAL-PRIMITIVE-TAG name))
(define (eval-closure formals body environment)
  (vector EVAL-CLOSURE-TAG formals body environment))

(define (eval-arity arguments count who)
  (if (eval-proper-list? arguments)
      (if (= (length arguments) count)
          #f
          (eval-error (string-append who ": wrong number of arguments")))
      (eval-error (string-append who ": improper argument list"))))

(define (eval-bind-formals formals arguments frame)
  (if (eval-core-symbol? formals)
      (begin
        (eval-define! (cons frame '()) (eval-name formals) arguments)
        #f)
      (if (null? formals)
          (if (null? arguments)
              #f
              (eval-error "procedure: too many arguments"))
          (if (not (pair? formals))
              (eval-error "lambda: malformed formals")
              (if (null? arguments)
                  (eval-error "procedure: too few arguments")
                  (if (not (eval-core-symbol? (car formals)))
                      (eval-error "lambda: formal is not an identifier")
                      (begin
                        (eval-define! (cons frame '())
                                      (eval-name (car formals))
                                      (car arguments))
                        (eval-bind-formals
                          (cdr formals) (cdr arguments) frame))))))))

(define (eval-sequence expressions environment continuation)
  (if (null? expressions)
      (continuation EVAL-UNSPECIFIED)
      (if (null? (cdr expressions))
          (eval-expression (car expressions) environment continuation)
          (eval-expression
            (car expressions) environment
            (lambda (value)
              (if (eval-error? value)
                  (continuation value)
                  (eval-sequence (cdr expressions)
                                 environment continuation)))))))

(define (eval-arguments expressions environment continuation)
  (if (null? expressions)
      (continuation '())
      (eval-expression
        (car expressions) environment
        (lambda (value)
          (eval-single
            value "application"
            (lambda (single)
              (if (eval-error? single)
                  (continuation single)
                  (eval-arguments
                    (cdr expressions) environment
                    (lambda (rest)
                      (if (eval-error? rest)
                          (continuation rest)
                          (continuation (cons single rest))))))))))))

(define (eval-fold-plus values result)
  (if (null? values) result
      (eval-fold-plus (cdr values) (+ result (car values)))))
(define (eval-fold-times values result)
  (if (null? values) result
      (eval-fold-times (cdr values) (* result (car values)))))
(define (eval-fold-minus values result)
  (if (null? values) result
      (eval-fold-minus (cdr values) (- result (car values)))))

(define (eval-binary-result name left right)
  (cond
    ((string=? name "=") (= left right))
    ((string=? name "<") (< left right))
    ((string=? name "<=") (<= left right))
    ((string=? name ">") (> left right))
    ((string=? name ">=") (>= left right))
    ((string=? name "cons") (cons left right))
    ((string=? name "eq?") (eq? left right))
    ((string=? name "eqv?") (eqv? left right))
    ((string=? name "string=?") (string=? left right))
    (else (eval-error (string-append "unknown binary primitive: " name)))))

(define (eval-primitive-binary name arguments continuation)
  (let ((error (eval-arity arguments 2 name)))
    (if error (continuation error)
        (continuation
          (eval-binary-result name
                              (car arguments)
                              (car (cdr arguments)))))))

(define (eval-apply-primitive name arguments environment continuation)
  (cond
    ((string=? name "+") (continuation (eval-fold-plus arguments 0)))
    ((string=? name "*") (continuation (eval-fold-times arguments 1)))
    ((string=? name "-")
     (if (null? arguments)
         (continuation (eval-error "-: needs an argument"))
         (if (null? (cdr arguments))
             (continuation (- 0 (car arguments)))
             (continuation
               (eval-fold-minus (cdr arguments) (car arguments))))))
    ((string=? name "=")
     (eval-primitive-binary name arguments continuation))
    ((string=? name "<")
     (eval-primitive-binary name arguments continuation))
    ((string=? name "<=")
     (eval-primitive-binary name arguments continuation))
    ((string=? name ">")
     (eval-primitive-binary name arguments continuation))
    ((string=? name ">=")
     (eval-primitive-binary name arguments continuation))
    ((string=? name "cons")
     (eval-primitive-binary name arguments continuation))
    ((string=? name "eq?")
     (eval-primitive-binary name arguments continuation))
    ((string=? name "eqv?")
     (eval-primitive-binary name arguments continuation))
    ((string=? name "string=?")
     (eval-primitive-binary name arguments continuation))
    ((string=? name "car")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error) (continuation (car (car arguments))))))
    ((string=? name "cdr")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error) (continuation (cdr (car arguments))))))
    ((string=? name "null?")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error) (continuation (null? (car arguments))))))
    ((string=? name "pair?")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error) (continuation (pair? (car arguments))))))
    ((string=? name "number?")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error) (continuation (number? (car arguments))))))
    ((string=? name "symbol?")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error) (continuation (symbol? (car arguments))))))
    ((string=? name "symbol->string")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (continuation (symbol->string (car arguments))))))
    ((string=? name "not")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (continuation (eq? (car arguments) #f)))))
    ((string=? name "list") (continuation arguments))
    ((string=? name "vector") (continuation (list->vector arguments)))
    ((string=? name "list->vector")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (if (eval-proper-list? (car arguments))
               (continuation (list->vector (car arguments)))
               (continuation
                 (eval-error "list->vector: expected a list"))))))
    ((string=? name "vector-length")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (if (vector? (car arguments))
               (continuation (vector-length (car arguments)))
               (continuation
                 (eval-error "vector-length: expected a vector"))))))
    ((string=? name "vector-ref")
     (let ((error (eval-arity arguments 2 name)))
       (if error (continuation error)
           (continuation
             (vector-ref (car arguments) (car (cdr arguments)))))))
    ((string=? name "vector-set!")
     (let ((error (eval-arity arguments 3 name)))
       (if error (continuation error)
           (begin
             (vector-set! (car arguments) (car (cdr arguments))
                          (car (cdr (cdr arguments))))
             (continuation EVAL-UNSPECIFIED)))))
    ((string=? name "values") (continuation (eval-values arguments)))
    ((string=? name "apply")
     (let ((error (eval-arity arguments 2 name)))
       (if error (continuation error)
           (if (eval-proper-list? (car (cdr arguments)))
               (eval-apply (car arguments) (car (cdr arguments))
                           environment continuation)
               (continuation (eval-error "apply: expected a proper list"))))))
    ((string=? name "call/cc")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (eval-apply (car arguments)
                       (list (eval-continuation continuation))
                       environment continuation))))
    ((string=? name "call-with-current-continuation")
     (eval-apply-primitive "call/cc" arguments environment continuation))
    ((string=? name "call-with-values")
     (let ((error (eval-arity arguments 2 name)))
       (if error (continuation error)
           (eval-apply
             (car arguments) '() environment
             (lambda (produced)
               (if (eval-error? produced)
                   (continuation produced)
                   (eval-apply
                     (car (cdr arguments))
                     (if (eval-values? produced)
                         (vector-ref produced 1)
                         (list produced))
                     environment continuation)))))))
    ((string=? name "npl_make_parameter")
     (if (= (length arguments) 2)
         (if (if (null? (car arguments)) #t
                 (eval-callable? (car arguments)))
             (continuation
               (vector EVAL-PARAMETER-TAG
                       (car arguments) (car (cdr arguments))))
             (continuation
               (eval-error "make-parameter: converter is not a procedure")))
         (continuation
           (eval-error "npl_make_parameter: wrong number of arguments"))))
    ((string=? name "npl_parameter_converter")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (if (eval-parameter? (car arguments))
               (let ((converter (vector-ref (car arguments) 1)))
                 (continuation (if (null? converter) #f converter)))
               (continuation (eval-error "parameterize: not a parameter"))))))
    ((string=? name "npl_parameters_enter")
     (let ((error (eval-arity arguments 3 name)))
       (if error (continuation error)
           (continuation
             (eval-parameter-enter
               (car arguments) (car (cdr arguments))
               (car (cdr (cdr arguments))))))))
    ((string=? name "npl_parameters_leave")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (continuation (eval-parameter-leave (car arguments))))))
    ((string=? name "with-exception-handler")
     (if (= (length arguments) 2)
         (if (if (eval-callable? (car arguments))
                 (eval-callable? (car (cdr arguments))) #f)
             (eval-with-handler (car arguments) (car (cdr arguments))
                                environment continuation)
             (continuation
               (eval-error "with-exception-handler: expected procedures")))
         (continuation
           (eval-error "with-exception-handler: wrong number of arguments"))))
    ((string=? name "raise")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (eval-raise-value (car arguments) #f environment continuation))))
    ((string=? name "raise-continuable")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (eval-raise-value (car arguments) #t environment continuation))))
    ((string=? name "dynamic-wind")
     (eval-dynamic-wind arguments environment continuation))
    ((string=? name "display")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (begin (display (car arguments))
                  (continuation EVAL-UNSPECIFIED)))))
    ((string=? name "write")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (begin (write (car arguments))
                  (continuation EVAL-UNSPECIFIED)))))
    ((string=? name "newline")
     (let ((error (eval-arity arguments 0 name)))
       (if error (continuation error)
           (begin (newline) (continuation EVAL-UNSPECIFIED)))))
    (else
     (continuation (eval-error (string-append "unknown primitive: " name))))))

(define (eval-apply procedure arguments environment continuation)
  (cond
    ((eval-error? procedure) (continuation procedure))
    ((eval-tagged? procedure EVAL-PRIMITIVE-TAG)
     (eval-apply-primitive (vector-ref procedure 1) arguments
                           environment continuation))
    ((eval-tagged? procedure EVAL-CLOSURE-TAG)
     (let ((frame (eval-frame)))
       (let ((error
               (eval-bind-formals (vector-ref procedure 1)
                                  arguments frame)))
         (if error
             (continuation error)
             (eval-sequence
               (vector-ref procedure 2)
               (eval-environment frame (vector-ref procedure 3))
               continuation)))))
    ((eval-tagged? procedure EVAL-CONTINUATION-TAG)
     (let ((error (eval-arity arguments 1 "continuation")))
       (if error (continuation error)
           (eval-restore-continuation procedure (car arguments)))))
    ((eval-parameter? procedure)
     (if (null? arguments)
         (continuation (eval-parameter-ref procedure))
         (if (null? (cdr arguments))
             (eval-parameter-update procedure (car arguments) continuation)
             (continuation
               (eval-error "parameter: expected zero or one argument")))))
    (else (continuation (eval-error "application: not a procedure")))))

(define (eval-if form environment continuation)
  (if (if (pair? (cdr form)) (pair? (cdr (cdr form))) #f)
      (eval-expression
        (car (cdr form)) environment
        (lambda (condition)
          (eval-single
            condition "if"
            (lambda (single)
              (if (eval-error? single)
                  (continuation single)
                  (if (eq? single #f)
                      (if (null? (cdr (cdr (cdr form))))
                          (continuation EVAL-UNSPECIFIED)
                          (eval-expression (car (cdr (cdr (cdr form))))
                                           environment continuation))
                      (eval-expression (car (cdr (cdr form)))
                                       environment continuation)))))))
      (continuation (eval-error "if: malformed form"))))

(define (eval-lambda form environment continuation)
  (continuation
    (eval-closure (car (cdr form))
                  (car (cdr (cdr form)))
                  environment)))

(define (eval-define-variable form environment continuation)
  (let ((target (car (cdr form)))
        (initializer (car (cdr (cdr form)))))
    (eval-expression
      initializer environment
      (lambda (value)
        (eval-single
          value "define"
          (lambda (single)
            (if (eval-error? single)
                (continuation single)
                (continuation
                  (eval-define! environment (eval-name target) single)))))))))

(define (eval-define-function form environment continuation)
  (let ((signature (car (cdr form)))
        (body (car (cdr (cdr form)))))
    (continuation
      (eval-define! environment
        (eval-name (car signature))
        (eval-closure (cdr signature) body environment)))))

(define (eval-set-form form environment continuation)
  (eval-expression
    (car (cdr (cdr form))) environment
    (lambda (value)
      (eval-single
        value "set!"
        (lambda (single)
          (if (eval-error? single)
              (continuation single)
              (continuation
                (eval-set! environment (car (cdr form)) single))))))))

(define (eval-application expression environment continuation)
  (eval-expression
    (car expression) environment
    (lambda (procedure)
      (eval-single
        procedure "application"
        (lambda (single)
          (if (eval-error? single)
              (continuation single)
              (eval-arguments
                (cdr expression) environment
                (lambda (arguments)
                  (if (eval-error? arguments)
                      (continuation arguments)
                      (eval-apply single arguments
                                  environment continuation))))))))))

(define (eval-and expressions environment continuation)
  (if (null? expressions)
      (continuation #t)
      (eval-expression
        (car expressions) environment
        (lambda (value)
          (eval-single value "and"
            (lambda (single)
              (if (if (eval-error? single) #t (eq? single #f))
                  (continuation single)
                  (if (null? (cdr expressions))
                      (continuation single)
                      (eval-and (cdr expressions)
                                environment continuation)))))))))

(define (eval-disjunction expressions environment continuation)
  (if (null? expressions)
      (continuation #f)
      (eval-expression
        (car expressions) environment
        (lambda (value)
          (eval-single value "or"
            (lambda (single)
              (if (eval-error? single)
                  (continuation single)
                  (if (eq? single #f)
                      (eval-disjunction (cdr expressions) environment continuation)
                      (continuation single)))))))))

(define (eval-binding-arguments bindings environment continuation)
  (if (null? bindings)
      (continuation '())
      (eval-expression
        (cdr (car bindings)) environment
        (lambda (value)
          (eval-single
            value "let"
            (lambda (single)
              (if (eval-error? single)
                  (continuation single)
                  (eval-binding-arguments
                    (cdr bindings) environment
                    (lambda (rest)
                      (if (eval-error? rest)
                          (continuation rest)
                          (continuation (cons single rest))))))))))))

(define (eval-binding-formals bindings)
  (if (null? bindings)
      '()
      (cons (car (car bindings))
            (eval-binding-formals (cdr bindings)))))

(define (eval-let form environment continuation)
  (let ((bindings (car (cdr form)))
        (body (car (cdr (cdr form)))))
    (eval-binding-arguments
      bindings environment
      (lambda (arguments)
        (if (eval-error? arguments)
            (continuation arguments)
            (eval-apply
              (eval-closure
                (eval-binding-formals bindings) body environment)
              arguments environment continuation))))))

(define (eval-letrec-initializers bindings environment continuation)
  (if (null? bindings)
      (continuation '())
      (eval-expression
        (cdr (car bindings)) environment
        (lambda (value)
          (eval-single value "letrec"
            (lambda (single)
              (if (eval-error? single)
                  (continuation single)
                  (eval-letrec-initializers
                    (cdr bindings) environment
                    (lambda (rest)
                      (if (eval-error? rest)
                          (continuation rest)
                          (continuation (cons single rest))))))))))))

(define (eval-letrec-assign! bindings values environment)
  (if (null? bindings)
      EVAL-UNSPECIFIED
      (let ((assigned
              (eval-set! environment (car (car bindings)) (car values))))
        (if (eval-error? assigned)
            assigned
            (eval-letrec-assign!
              (cdr bindings) (cdr values) environment)))))

(define (eval-predeclare-bindings bindings environment)
  (if (null? bindings)
      EVAL-UNSPECIFIED
      (begin
        (eval-define! environment (eval-name (car (car bindings)))
                      EVAL-UNSPECIFIED)
        (eval-predeclare-bindings (cdr bindings) environment))))

(define (eval-letrec form environment continuation)
  (let ((bindings (car (cdr form)))
        (body (car (cdr (cdr form))))
        (recursive-environment
          (eval-environment (eval-frame) environment)))
    (eval-predeclare-bindings bindings recursive-environment)
    (eval-letrec-initializers bindings recursive-environment
      (lambda (values)
        (if (eval-error? values)
            (continuation values)
            (let ((assigned
                    (eval-letrec-assign!
                      bindings values recursive-environment)))
              (if (eval-error? assigned)
                  (continuation assigned)
                  (eval-sequence
                    body recursive-environment continuation))))))))

(define (eval-core-literal expression continuation)
  (let ((tag (car expression)))
    (cond
      ((= tag 101) (continuation (cdr expression)))
      ((= tag 102) (continuation (integer->char (cdr expression))))
      ((= tag 103)
       (continuation
         (list->vector
           (frontend-datum->runtime (car (cdr expression))))))
      ((= tag 104)
       (continuation
         (frontend-bytevector-items->runtime
           (car (cdr expression)))))
      ((= tag 108) (continuation #t))
      ((= tag 109) (continuation #f))
      ((= tag 204)
       (continuation
         (frontend-datum->runtime (car (cdr expression)))))
      (else (continuation (eval-error "unknown core literal"))))))

(define (eval-expression expression environment continuation)
  (cond
    ((eval-core-symbol? expression)
     (continuation (eval-lookup environment expression)))
    ((not (pair? expression)) (continuation expression))
    ((eval-core-tagged? expression 101)
     (eval-core-literal expression continuation))
    ((eval-core-tagged? expression 102)
     (eval-core-literal expression continuation))
    ((eval-core-tagged? expression 103)
     (eval-core-literal expression continuation))
    ((eval-core-tagged? expression 104)
     (eval-core-literal expression continuation))
    ((eval-core-tagged? expression 108)
     (eval-core-literal expression continuation))
    ((eval-core-tagged? expression 109)
     (eval-core-literal expression continuation))
    ((eval-core-tagged? expression 204)
     (eval-core-literal expression continuation))
    ((eval-core-tagged? expression 200)
     (eval-define-variable expression environment continuation))
    ((eval-core-tagged? expression 211)
     (eval-define-function expression environment continuation))
    ((eval-core-tagged? expression 212)
     (eval-lambda expression environment continuation))
    ((eval-core-tagged? expression 213)
     (eval-if expression environment continuation))
    ((eval-core-tagged? expression 214)
     (eval-let expression environment continuation))
    ((eval-core-tagged? expression 216)
     (eval-sequence (cdr expression) environment continuation))
    ((eval-core-tagged? expression 217)
     (eval-set-form expression environment continuation))
    ((eval-core-tagged? expression 221)
     (eval-letrec expression environment continuation))
    ((number? (car expression))
     (continuation (eval-error "unsupported core expression")))
    ((not (eval-proper-list? expression))
     (continuation (eval-error "malformed application")))
    (else (eval-application expression environment continuation))))

(define EVAL-PRIMITIVE-NAMES
  (list "+" "-" "*" "=" "<" "<=" ">" ">="
        "cons" "car" "cdr" "list" "vector" "list->vector"
        "vector-length" "vector-ref" "vector-set!" "null?" "pair?"
        "eq?" "eqv?" "not" "number?" "symbol?" "symbol->string"
        "string=?" "apply" "values" "call-with-values"
        "call/cc" "call-with-current-continuation"
        "with-exception-handler" "raise" "raise-continuable"
        "dynamic-wind" "npl_make_parameter" "npl_parameter_converter"
        "npl_parameters_enter" "npl_parameters_leave"
        "display" "write" "newline"))

(define (eval-install-primitives environment names)
  (if (null? names)
      environment
      (begin
        (eval-define! environment (car names) (eval-primitive (car names)))
        (eval-install-primitives environment (cdr names)))))

(define (set-eval-standard-root! root)
  (frontend-set-standard-root! root))

(define (make-eval-environment)
  (set! EVAL-CURRENT-WIND '())
  (set! EVAL-CURRENT-HANDLER '())
  (set! EVAL-CURRENT-PARAMETERS '())
  (let ((environment (eval-environment (eval-frame) '())))
    (eval-install-primitives environment EVAL-PRIMITIVE-NAMES)
    (eval-define! environment "%frontend-session" (make-frontend-session))
    environment))

(define (eval-in-environment expression environment)
  (let ((frontend-session
          (car (cdr (eval-find-binding "%frontend-session" environment)))))
    (let ((core (frontend-expand-datum frontend-session expression)))
      (if (frontend-error? core)
          (eval-error (frontend-error-message core))
          (eval-sequence core environment (lambda (value) value))))))
