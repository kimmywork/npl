; Evaluator entry points: the primitive name registry, environment
; construction, eval and load over a session, and environment specifiers for
; (scheme eval). Expression evaluation itself is in evaluator-core.scm.

(define EVAL-PRIMITIVE-NAMES
  (list "+" "-" "*" "=" "<" "<=" ">" ">="
        "cons" "car" "cdr" "set-car!" "set-cdr!" "list" "vector" "list->vector"
        "vector-length" "vector-ref" "vector-set!" "vector?"
        "npl_inexact_p" "npl_inexact_op" "npl_inexact_binary" "npl_finite_p" "npl_nan_p" "npl_infinite_p"
        "npl_exact_to_inexact" "npl_inexact_to_exact"
        "npl_number_div" "npl_integer_p" "npl_rational_numerator"
        "npl_rational_denominator" "npl_number_from_string"
        "npl_number_from_string_radix" "npl_number_to_string_radix"
        "npl_complex_make" "npl_complex_p" "npl_complex_real" "npl_complex_imag"
        "npl_number_zero_p" "npl_number_gcd" "npl_number_quotient"
        "npl_number_remainder" "string?"
        "null?" "pair?"
        "eq?" "eqv?" "not" "number?" "number->string" "symbol?" "symbol->string" "string->symbol" "procedure?"
        "string-byte-ref" "npl_make_string" "string-set!" "npl_string_fill"
        "append" "bytevector?" "bytevector" "bytevector-length" "bytevector-u8-ref"
        "bytevector-u8-set!"
        "open-input-string"
        "string-length" "string-append" "string-ref" "substring" "int->string"
        "npl_lex_class_w" "npl_lex_scan_sym_end" "npl_lex_number"
        "string=?" "npl_list_to_values" "apply" "values" "call-with-values"
        "call/cc" "call-with-current-continuation"
        "with-exception-handler" "raise" "raise-continuable"
        "dynamic-wind" "npl_make_parameter" "npl_parameter_converter"
        "npl_parameters_enter" "npl_parameters_leave" "npl_promise_op"
        "%eval-make-environment" "%eval-evaluate" "%eval-load-file"
        "display" "write" "newline" "read" "eof-object?"
        "open-input-file" "open-output-file" "open-output-string"
        "get-output-string"
        "close-port" "close-input-port" "output-port-open?"
        "delete-file" "input-port-open?" "read-char" "write-char"
        "char?" "char->integer" "integer->char"
        "command-line" "exit" "get-environment-variable"
        "get-environment-variables"
        "npl_make_record" "npl_record_p" "npl_record_ref" "npl_record_set"
        "npl_try_open_file" "read-result" "current-input-port" "file-exists?"
        "npl_port_parameter"
        "input-port?" "output-port?" "textual-port?" "binary-port?" "port?"
        "eof-object" "close-output-port" "open-input-bytevector"
        "open-output-bytevector" "get-output-bytevector"
        "open-binary-input-file" "open-binary-output-file" "read-bytevector"
        "read-bytevector!" "write-bytevector" "peek-char" "read-u8" "peek-u8"
        "write-u8" "flush-output-port" "write-shared" "write-simple"
        "npl_port_ready_p" "npl_jiffies_per_second" "npl_current_jiffy"
        "npl_current_second"))

(define (eval-install-primitives environment names)
  (if (null? names)
      environment
      (begin
        (eval-define! environment (car names) (eval-primitive (car names)))
        (eval-install-primitives environment (cdr names)))))

(define (set-eval-standard-root! root)
  (frontend-set-standard-root! root))

(define EVAL-RUNTIME-LIBRARY-EXPORTS
  (list
    (cons "make-environment" "%eval-make-environment")
    (cons "evaluate" "%eval-evaluate")
    (cons "load-file" "%eval-load-file")))

(define (make-eval-environment)
  (let ((frontend-session (make-frontend-session)))
    (frontend-register-runtime-library!
      frontend-session "npl/eval-runtime"
      EVAL-RUNTIME-LIBRARY-EXPORTS)
    (let ((environment
            (eval-environment (eval-frame) '() #t
                              frontend-session (eval-session))))
      (eval-install-primitives environment EVAL-PRIMITIVE-NAMES)
      environment)))

(define (eval-load-file-forms port environment)
  (let ((form (read port)))
    (if (eof-object? form)
        (begin
          (close-input-port port)
          EVAL-UNSPECIFIED)
        (let ((result (eval-in-environment form environment)))
          (if (eval-error? result)
              (begin
                (close-input-port port)
                result)
              (eval-load-file-forms port environment))))))

(define (eval-load-file filename environment)
  (eval-load-file-forms (open-input-file filename) environment))

(define (eval-definition? expression)
  (if (pair? expression)
      (if (symbol? (car expression))
          (let ((name (symbol->string (car expression))))
            (if (string=? name "define") #t
                (string=? name "define-syntax")))
          #f)
      #f))

(define (eval-in-environment expression environment)
  (if (not (eval-environment? environment))
      (eval-error "eval: expected an environment")
      (if (if (not (eval-environment-mutable? environment))
              (eval-definition? expression) #f)
          (eval-error "eval: immutable environment")
          (let ((session (eval-environment-session environment)))
            (if (not session)
                (eval-error "eval: invalid environment")
                (let ((previous (eval-current-session)))
                  (eval-activate-session! session)
                  (set! EVAL-CURRENT-EVALUATION-ENVIRONMENT environment)
                  (let ((frontend-session
                          (eval-environment-frontend-session environment)))
                    (let ((core
                            (frontend-expand-datum
                              frontend-session expression)))
                      (let ((result
                              (if (frontend-error? core)
                                  (eval-error (frontend-error-message core))
                                  (eval-sequence
                                    core environment (lambda (value) value)))))
                        (eval-capture-session! session)
                        (eval-activate-session! previous)
                        result)))))))))

(define (eval-apply-in-environment procedure arguments environment)
  (if (if (eval-environment? environment) (eval-callable? procedure) #f)
      (let ((session (eval-environment-session environment))
            (previous (eval-current-session)))
        (eval-activate-session! session)
        (let ((result
                (eval-apply procedure arguments
                            environment (lambda (value) value))))
          (eval-capture-session! session)
          (eval-activate-session! previous)
          result))
      (eval-error "eval: expected a procedure and environment")))

(define (make-eval-environment-from-imports import-sets)
  (if (not (eval-proper-list? import-sets))
      (eval-error "environment: expected import sets")
      (let ((bootstrap (make-eval-environment)))
        (let ((environment
                (eval-environment
                  (eval-frame) bootstrap #t
                  (eval-environment-frontend-session bootstrap)
                  (eval-environment-session bootstrap))))
          (frontend-reset-interaction-bindings!
            (eval-environment-frontend-session environment))
          (let ((result
                  (eval-in-environment
                    (cons 'import import-sets) environment)))
            (if (eval-error? result)
                result
                (begin
                  (frontend-require-bound!
                    (eval-environment-frontend-session environment))
                  (eval-environment-freeze! environment)
                  environment)))))))
