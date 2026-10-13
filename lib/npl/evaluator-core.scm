; Shared persistent evaluator used by the REPL and (scheme eval).

; Raw numeric mechanisms used by the evaluator. Public numeric policy remains
; in the scheme libraries; keeping this bridge in the shared source preserves
; visibility to the compiler's single-file library include path.
(define (eval-exact-numeric-name? name)
  (cond
    ((string=? name "npl_number_div") #t)
    ((string=? name "npl_inexact_p") #t)
    ((string=? name "npl_inexact_op") #t)
    ((string=? name "npl_inexact_binary") #t)
    ((string=? name "npl_finite_p") #t)
    ((string=? name "npl_nan_p") #t)
    ((string=? name "npl_infinite_p") #t)
    ((string=? name "npl_exact_to_inexact") #t)
    ((string=? name "npl_inexact_to_exact") #t)
    ((string=? name "npl_integer_p") #t)
    ((string=? name "npl_rational_numerator") #t)
    ((string=? name "npl_rational_denominator") #t)
    ((string=? name "string?") #t)
    ((string=? name "npl_number_from_string") #t)
    ((string=? name "npl_number_from_string_radix") #t)
    ((string=? name "npl_number_to_string_radix") #t)
    ((string=? name "npl_complex_make") #t)
    ((string=? name "npl_complex_p") #t)
    ((string=? name "npl_complex_real") #t)
    ((string=? name "npl_complex_imag") #t)
    ((string=? name "npl_number_zero_p") #t)
    ((string=? name "npl_number_gcd") #t)
    ((string=? name "npl_number_quotient") #t)
    (else (string=? name "npl_number_remainder"))))
(define (eval-exact-numeric name arguments continuation)
  (let ((error (eval-arity arguments
                          (cond
                            ((string=? name "npl_inexact_op") 2)
                            ((string=? name "npl_inexact_binary") 3)
                            ((string=? name "npl_number_div") 2)
                            ((string=? name "npl_number_from_string_radix") 2)
                            ((string=? name "npl_number_to_string_radix") 2)
                            ((string=? name "npl_complex_make") 2)
                            ((string=? name "npl_number_gcd") 2)
                            ((string=? name "npl_number_quotient") 2)
                            ((string=? name "npl_number_remainder") 2)
                            (else 1))
                          name)))
    (if error (continuation error)
        (let ((value (car arguments)))
          (cond
            ((string=? name "string?")
             (continuation (string? value)))
            ((string=? name "npl_inexact_p")
             (continuation (npl_inexact_p value)))
            ((string=? name "npl_inexact_op")
             (continuation (npl_inexact_op (car arguments) (car (cdr arguments)))))
            ((string=? name "npl_inexact_binary")
             (continuation (npl_inexact_binary (car arguments)
                                               (car (cdr arguments))
                                               (car (cdr (cdr arguments))))))
            ((string=? name "npl_finite_p")
             (continuation (npl_finite_p value)))
            ((string=? name "npl_nan_p")
             (continuation (npl_nan_p value)))
            ((string=? name "npl_infinite_p")
             (continuation (npl_infinite_p value)))
            ((string=? name "npl_exact_to_inexact")
             (continuation (npl_exact_to_inexact value)))
            ((string=? name "npl_inexact_to_exact")
             (continuation (npl_inexact_to_exact value)))
            ((string=? name "npl_integer_p")
             (continuation (npl_integer_p value)))
            ((string=? name "npl_number_from_string")
             (continuation (npl_number_from_string value)))
            ((string=? name "npl_number_from_string_radix")
             (continuation (npl_number_from_string_radix
                             value (car (cdr arguments)))))
            ((string=? name "npl_number_to_string_radix")
             (continuation (npl_number_to_string_radix
                             value (car (cdr arguments)))))
            ((string=? name "npl_complex_make")
             (continuation (npl_complex_make value (car (cdr arguments)))))
            ((string=? name "npl_complex_p")
             (continuation (npl_complex_p value)))
            ((string=? name "npl_complex_real")
             (continuation (npl_complex_real value)))
            ((string=? name "npl_complex_imag")
             (continuation (npl_complex_imag value)))
            ((string=? name "npl_number_zero_p")
             (continuation (npl_number_zero_p value)))
            ((string=? name "npl_number_gcd")
             (continuation (npl_number_gcd value (car (cdr arguments)))))
            ((string=? name "npl_number_quotient")
             (continuation (npl_number_quotient value (car (cdr arguments)))))
            ((string=? name "npl_number_remainder")
             (continuation (npl_number_remainder value (car (cdr arguments)))))
            ((not (number? value))
             (continuation (eval-error "numeric primitive: expected a number")))
            ((string=? name "npl_rational_numerator")
             (continuation (npl_rational_numerator value)))
            ((string=? name "npl_rational_denominator")
             (continuation (npl_rational_denominator value)))
            ((not (number? (car (cdr arguments))))
             (continuation (eval-error "numeric primitive: expected a number")))
            ; Only an exact zero divisor is an error; inexact zero follows IEEE.
            ((if (npl_number_zero_p (car (cdr arguments)))
                 (not (npl_inexact_p (npl_complex_real (car (cdr arguments)))))
                 #f)
             (continuation (eval-error "/: division by zero")))
            (else (continuation (npl_number_div value (car (cdr arguments))))))))))

; Identity-tagged objects cannot collide with ordinary user vectors.
(define EVAL-TAG-OWNER (cons #f #f))
(define EVAL-PRIMITIVE-TAG (cons EVAL-TAG-OWNER 901))
(define EVAL-CLOSURE-TAG (cons EVAL-TAG-OWNER 902))
(define EVAL-ERROR-TAG (cons EVAL-TAG-OWNER 903))
(define EVAL-VALUES-TAG (cons EVAL-TAG-OWNER 904))
(define EVAL-CONTINUATION-TAG (cons EVAL-TAG-OWNER 905))
(define EVAL-PARAMETER-TAG (cons EVAL-TAG-OWNER 907))
(define EVAL-WIND-TAG (cons EVAL-TAG-OWNER 908))
(define EVAL-HANDLER-TAG (cons EVAL-TAG-OWNER 909))
(define EVAL-PARAMETER-BINDING-TAG (cons EVAL-TAG-OWNER 910))
(define EVAL-PROMISE-TAG (cons EVAL-TAG-OWNER 912))
(define EVAL-ENVIRONMENT-TAG (cons EVAL-TAG-OWNER 913))
(define EVAL-SESSION-TAG (cons EVAL-TAG-OWNER 914))
(define EVAL-UNSPECIFIED (vector 906))

(define EVAL-PROMISE-DONE 0)
(define EVAL-PROMISE-DELAY 1)
(define EVAL-PROMISE-CHAIN 2)
(define EVAL-PROMISE-REDIRECT 3)

(define EVAL-CURRENT-WIND '())
(define EVAL-CURRENT-HANDLER '())
; Environment of the primitive application in progress, for raising its
; error through the handlers (eval-primitive-error).
(define EVAL-PRIMITIVE-ENVIRONMENT '())
(define EVAL-CURRENT-PARAMETERS '())
; Evaluator parameter objects for the current ports (R7RS 6.13.1), shared
; by every environment of the process; initial values are the host ports.
(define EVAL-PORT-PARAMETERS
  (vector (vector EVAL-PARAMETER-TAG '() ((npl_port_parameter 0)))
          (vector EVAL-PARAMETER-TAG '() ((npl_port_parameter 1)))
          (vector EVAL-PARAMETER-TAG '() ((npl_port_parameter 2)))))
(define (eval-current-port kind)
  (eval-parameter-ref (vector-ref EVAL-PORT-PARAMETERS kind)))
(define EVAL-CURRENT-EVALUATION-ENVIRONMENT '())

(define (eval-session)
  (vector EVAL-SESSION-TAG '() '() '()))

(define (eval-current-session)
  (vector EVAL-SESSION-TAG EVAL-CURRENT-WIND
          EVAL-CURRENT-HANDLER EVAL-CURRENT-PARAMETERS))

(define (eval-capture-session! session)
  (vector-set! session 1 EVAL-CURRENT-WIND)
  (vector-set! session 2 EVAL-CURRENT-HANDLER)
  (vector-set! session 3 EVAL-CURRENT-PARAMETERS)
  session)

(define (eval-activate-session! session)
  (set! EVAL-CURRENT-WIND (vector-ref session 1))
  (set! EVAL-CURRENT-HANDLER (vector-ref session 2))
  (set! EVAL-CURRENT-PARAMETERS (vector-ref session 3))
  session)

(define (eval-tagged? value tag)
  (if (vector? value)
      (if (> (vector-length value) 0)
          (eq? (vector-ref value 0) tag)
          #f)
      #f))

(define (eval-error message) (vector EVAL-ERROR-TAG message))
(define (eval-error? value) (eval-tagged? value EVAL-ERROR-TAG))
(define (eval-error-message value) (vector-ref value 1))
(define (eval-unspecified? value) (eq? value EVAL-UNSPECIFIED))
(define (eval-values values) (vector EVAL-VALUES-TAG values))
(define (eval-list-to-values values)
  (if (not (eval-proper-list? values))
      (eval-error "npl_list_to_values: expected a proper list")
      (if (null? values)
          (eval-values values)
          (if (null? (cdr values))
              (car values)
              (eval-values values)))))
(define (eval-values? value) (eval-tagged? value EVAL-VALUES-TAG))
(define (eval-values-list value) (vector-ref value 1))
(define (eval-continuation continuation)
  (vector EVAL-CONTINUATION-TAG continuation
          EVAL-CURRENT-WIND EVAL-CURRENT-HANDLER
          EVAL-CURRENT-PARAMETERS))

(define (eval-parameter? value) (eval-tagged? value EVAL-PARAMETER-TAG))
(define (eval-parameter-binding? value)
  (eval-tagged? value EVAL-PARAMETER-BINDING-TAG))
(define (eval-promise? value) (eval-tagged? value EVAL-PROMISE-TAG))
(define (eval-promise-state promise) (vector-ref promise 1))
(define (eval-promise-value promise) (vector-ref promise 2))
(define (eval-promise-set! promise state value)
  (vector-set! promise 1 state)
  (vector-set! promise 2 value))

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

; Lexical environments carry their frame, parent, and mutability.
(define (eval-frame) (cons '() '()))
(define (eval-environment frame parent mutable frontend-session evaluator-session)
  (vector EVAL-ENVIRONMENT-TAG frame parent mutable
          frontend-session evaluator-session))
(define (eval-environment? value)
  (eval-tagged? value EVAL-ENVIRONMENT-TAG))
(define (eval-environment-frame environment) (vector-ref environment 1))
(define (eval-environment-parent environment) (vector-ref environment 2))
(define (eval-environment-mutable? environment) (vector-ref environment 3))
(define (eval-environment-frontend-session environment)
  (let ((session (vector-ref environment 4)))
    (if session session
        (eval-environment-frontend-session
          (eval-environment-parent environment)))))
(define (eval-environment-session environment)
  (let ((session (vector-ref environment 5)))
    (if session session
        (eval-environment-session
          (eval-environment-parent environment)))))
(define (eval-environment-freeze! environment)
  (if (null? environment)
      #f
      (begin
        (vector-set! environment 3 #f)
        (eval-environment-freeze!
          (eval-environment-parent environment)))))

(define (eval-frame-find name bindings)
  (if (null? bindings)
      #f
      (if (string=? name (car (car bindings)))
          (car bindings)
          (eval-frame-find name (cdr bindings)))))

(define (eval-find-binding-owner name environment)
  (if (null? environment)
      #f
      (let ((binding
              (eval-frame-find
                name (car (eval-environment-frame environment)))))
        (if binding environment
            (eval-find-binding-owner
              name (eval-environment-parent environment))))))

(define (eval-find-binding name environment)
  (let ((owner (eval-find-binding-owner name environment)))
    (if owner
        (eval-frame-find name (car (eval-environment-frame owner)))
        #f)))

(define (eval-define! environment name value)
  (if (not (eval-environment-mutable? environment))
      (eval-error "eval: immutable environment")
      (let ((frame (eval-environment-frame environment)))
        (let ((binding (eval-frame-find name (car frame))))
          (if binding
              (set-car! (cdr binding) value)
              (set-car! frame
                        (cons (cons name (cons value '())) (car frame)))))
        EVAL-UNSPECIFIED)))

(define (eval-lookup environment identifier)
  (let ((name (eval-name identifier)))
    (let ((binding (eval-find-binding name environment)))
      (if binding
          (car (cdr binding))
          (eval-error (string-append "unbound identifier: " name))))))

(define (eval-set! environment identifier value)
  (let ((name (eval-name identifier)))
    (let ((owner (eval-find-binding-owner name environment)))
      (if owner
          (if (eval-environment-mutable? owner)
              (let ((binding (eval-frame-find
                               name (car (eval-environment-frame owner)))))
                (set-car! (cdr binding) value)
                EVAL-UNSPECIFIED)
              (eval-error "eval: immutable binding"))
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

(define (eval-arity-error? error)
  (let ((message (eval-error-message error)))
    (if (string=? message "procedure: too many arguments")
        #t
        (string=? message "procedure: too few arguments"))))

(define (eval-bind-formals formals arguments frame)
  (if (eval-core-symbol? formals)
      (begin
        (eval-define! (eval-environment frame '() #t #f #f)
                      (eval-name formals) arguments)
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
                        (eval-define! (eval-environment frame '() #t #f #f)
                                      (eval-name (car formals))
                                      (car arguments))
                        (eval-bind-formals
                          (cdr formals) (cdr arguments) frame))))))))

(define (eval-body expressions environment continuation)
  (eval-expression (frontend-core-body-expression expressions)
                   environment continuation))

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

(define (eval-number-list? values)
  (if (null? values)
      #t
      (if (number? (car values))
          (eval-number-list? (cdr values))
          #f)))

(define (eval-number-binary-result name left right)
  (cond
    ((string=? name "=") (= left right))
    ((string=? name "<") (< left right))
    ((string=? name "<=") (<= left right))
    ((string=? name ">") (> left right))
    ((string=? name ">=") (>= left right))
    (else (eval-error (string-append "unknown numeric primitive: " name)))))

(define (eval-number-binary? name left right)
  (if (if (number? left) (number? right) #f)
      (eval-number-binary-result name left right)
      (eval-error (string-append name ": expected numbers"))))

(define (eval-number-comparison name arguments continuation)
  (if (< (length arguments) 2)
      (continuation
        (eval-error (string-append name ": needs at least two arguments")))
      (if (eval-number-list? arguments)
          (continuation (eval-compare-adjacent name arguments))
          (continuation
            (eval-error (string-append name ": expected numbers"))))))

(define (eval-compare-adjacent name arguments)
  (if (null? (cdr arguments))
      #t
      (if (eval-number-binary-result
            name (car arguments) (car (cdr arguments)))
          (eval-compare-adjacent name (cdr arguments))
          #f)))

(define (eval-user-vector? value)
  (if (not (vector? value))
      #f
      (if (eval-unspecified? value)
          #f
          (if (= (vector-length value) 0)
              #t
              (let ((tag (vector-ref value 0)))
                (if (pair? tag)
                    (not (eq? (car tag) EVAL-TAG-OWNER))
                    #t))))))

(define (eval-vector-index? value index)
  (if (eval-user-vector? value)
      (if (number? index)
          (if (if (>= index 0) (< index (vector-length value)) #f)
              #t
              #f)
          #f)
      #f))

(define (eval-binary-result name left right)
  (cond
    ((string=? name "=") (eval-number-binary? name left right))
    ((string=? name "<") (eval-number-binary? name left right))
    ((string=? name "<=") (eval-number-binary? name left right))
    ((string=? name ">") (eval-number-binary? name left right))
    ((string=? name ">=") (eval-number-binary? name left right))
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

(define (eval-apply procedure arguments environment continuation)
  (cond
    ((eval-error? procedure) (continuation procedure))
    ((eval-tagged? procedure EVAL-PRIMITIVE-TAG)
     (set! EVAL-PRIMITIVE-ENVIRONMENT environment)
     (eval-apply-primitive (vector-ref procedure 1) arguments
                           environment continuation))
    ((eval-tagged? procedure EVAL-CLOSURE-TAG)
     (let ((frame (eval-frame)))
       (let ((error
               (eval-bind-formals (vector-ref procedure 1)
                                  arguments frame)))
         (if error
             (if (eval-arity-error? error)
                 (begin
                   (set! EVAL-PRIMITIVE-ENVIRONMENT environment)
                   (eval-primitive-error
                     "procedure" "wrong number of arguments"
                     (length arguments) continuation))
                 (continuation error))
             (eval-body
               (vector-ref procedure 2)
               (eval-environment frame (vector-ref procedure 3)
                                 #t #f #f)
               continuation)))))
    ((eval-tagged? procedure EVAL-CONTINUATION-TAG)
     (eval-restore-continuation procedure (eval-list-to-values arguments)))
    ((eval-parameter? procedure)
     (if (null? arguments)
         (continuation (eval-parameter-ref procedure))
         (if (null? (cdr arguments))
             (eval-parameter-update procedure (car arguments) continuation)
             (continuation
               (eval-error "parameter: expected zero or one argument")))))
    ((eval-promise? procedure)
     (if (null? arguments)
         (eval-force procedure environment continuation)
         (continuation (eval-error "force: expected zero arguments"))))
    ; A host (native) procedure passed in from the embedding program, for
    ; example by eval-apply-in-environment arguments: apply it directly;
    ; its errors re-raise through the evaluator's handlers.
    ((procedure? procedure)
     (set! EVAL-PRIMITIVE-ENVIRONMENT environment)
     (eval-host-primitive (lambda () (apply procedure arguments)) continuation))
    (else
     (set! EVAL-PRIMITIVE-ENVIRONMENT environment)
     (eval-primitive-error "application" "not a procedure" procedure
                           continuation))))

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

(define (eval-binding-arguments bindings environment who continuation)
  (if (null? bindings)
      (continuation '())
      (eval-expression
        (cdr (car bindings)) environment
        (lambda (value)
          (eval-single
            value who
            (lambda (single)
              (if (eval-error? single)
                  (continuation single)
                  (eval-binding-arguments
                    (cdr bindings) environment who
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
      bindings environment "let"
      (lambda (arguments)
        (if (eval-error? arguments)
            (continuation arguments)
            (eval-apply
              (eval-closure
                (eval-binding-formals bindings) body environment)
              arguments environment continuation))))))

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

(define (eval-initialize-bindings bindings environment continuation)
  (if (null? bindings)
      (continuation EVAL-UNSPECIFIED)
      (eval-expression (cdr (car bindings)) environment
        (lambda (value)
          (eval-single value "letrec*"
            (lambda (single)
              (if (eval-error? single)
                  (continuation single)
                  (let ((assigned
                          (eval-set! environment (car (car bindings)) single)))
                    (if (eval-error? assigned)
                        (continuation assigned)
                        (eval-initialize-bindings
                          (cdr bindings) environment continuation))))))))))

(define (eval-letrec form environment sequential continuation)
  (let ((bindings (car (cdr form)))
        (body (car (cdr (cdr form))))
        (recursive-environment
          (eval-environment (eval-frame) environment #t #f #f)))
    (eval-predeclare-bindings bindings recursive-environment)
    (let ((finish
            (lambda (assigned)
              (if (eval-error? assigned)
                  (continuation assigned)
                  (eval-body body recursive-environment continuation)))))
      (if sequential
          (eval-initialize-bindings bindings recursive-environment finish)
          (eval-binding-arguments bindings recursive-environment "letrec"
            (lambda (values)
              (if (eval-error? values)
                  (continuation values)
                  (finish
                    (eval-letrec-assign!
                      bindings values recursive-environment)))))))))

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
    ((eval-core-tagged? expression 220)
     (eval-expression
       (frontend-expand-core-letstar
         (car (cdr expression)) (car (cdr (cdr expression))))
       environment continuation))
    ((eval-core-tagged? expression 221)
     (eval-letrec expression environment #f continuation))
    ((eval-core-tagged? expression 222)
     (eval-letrec expression environment #t continuation))
    ((number? (car expression))
     (continuation (eval-error "unsupported core expression")))
    ((not (eval-proper-list? expression))
     (continuation (eval-error "malformed application")))
    (else (eval-application expression environment continuation))))
