; Evaluator entry points: environments, eval, load, and error text. Each
; environment owns a global table and a frontend session; forms are expanded
; by the shared frontend, compiled once (evaluator-compile.scm), and run.
; Errors are raised as R7RS error objects, as in compiled code.

; Native values of the libraries compiled into npli: the (npl primitive)
; names, generated from the shared primitive table, and every definition of
; the standard libraries below, each with a checksum of its source. Entry:
; (key checksum (name . value) ...). (scheme eval), (scheme load),
; (scheme repl), and (scheme r5rs) stay evaluated from source: their
; procedures act on the running evaluator's environments.
(define EVAL-NATIVE-LIBRARIES
  (%library-definitions
    (npl primitive) (scheme base) (scheme case-lambda) (scheme char)
    (scheme complex) (scheme cxr) (scheme file) (scheme inexact)
    (scheme lazy) (scheme process-context) (scheme read) (scheme time)
    (scheme write)))

(define (eval-native-library key libraries)
  (cond
    ((null? libraries) #f)
    ((string=? key (car (car libraries))) (car libraries))
    (else (eval-native-library key (cdr libraries)))))

(define (eval-native-definitions key)
  (cdr (cdr (eval-native-library key EVAL-NATIVE-LIBRARIES))))

(define (eval-native-names-match? definitions natives)
  (cond
    ((null? definitions) (null? natives))
    ((null? natives) #f)
    ((string=? (car (car definitions)) (car (car natives)))
     (eval-native-names-match? (cdr definitions) (cdr natives)))
    (else #f)))

(define (eval-native-library-match? library checksum definitions)
  (if (eval-native-names-match? definitions (cdr (cdr library)))
      (= (checksum) (car (cdr library)))
      #f))

; Frontend hook (src/compiler/frontend/library-definitions.npl): a library
; loaded from source is bound natively when npli was compiled from the same
; source, so its definitions and checksum agree.
(define (eval-native-match? key checksum definitions)
  (let ((library (eval-native-library key EVAL-NATIVE-LIBRARIES)))
    (if library
        (eval-native-library-match? library checksum definitions)
        #f)))

(frontend-set-native-hook! eval-native-match?)

; Declared C primitives (src/compiler/frontend/c-primitives.npl): the
; frontend names each declared C function by a row name, which is bound in
; the environment being expanded to a procedure calling the function. The
; symbol resolves through npli itself and the libraries loaded with
; npli -l (runtime/ffi.c).
(define-c-primitive (eval-ffi-procedure (symbol string) (count fixnum)) any
  "npl_ffi_procedure")

(define EVAL-FFI-MAX-ARGUMENTS 8)

; The environment whose forms the frontend is expanding. Expansion does not
; run evaluated code (macros are syntax-rules), so it does not nest.
(define EVAL-EXPANSION-ENVIRONMENT (list #f))

(define (eval-bind-c-procedure! name procedure)
  (set-car! (eval-environment-cell (car EVAL-EXPANSION-ENVIRONMENT) name)
            procedure)
  #f)

(define (eval-c-procedure-problem symbol procedure name)
  (if procedure
      (eval-bind-c-procedure! name procedure)
      (string-append "C function not found: " symbol
                     " (load its library with npli -l PATH)")))

; Returns #f once name is bound, else the reason it cannot be.
(define (eval-bind-c-primitive! name symbol count)
  (if (> count EVAL-FFI-MAX-ARGUMENTS)
      (string-append "npli calls C functions of at most 8 arguments: "
                     symbol)
      (eval-c-procedure-problem symbol (eval-ffi-procedure symbol count)
                                name)))

(frontend-set-c-primitive-hook! eval-bind-c-primitive!)

(define (eval-expand expression environment)
  (let ((previous (car EVAL-EXPANSION-ENVIRONMENT)))
    (set-car! EVAL-EXPANSION-ENVIRONMENT environment)
    (let ((forms (frontend-expand-datum
                   (eval-environment-frontend-session environment)
                   expression)))
      (set-car! EVAL-EXPANSION-ENVIRONMENT previous)
      forms)))

; (procedure . opcode) for (scheme base)'s numeric procedures, as bound by
; eval-bind-native!; opcodes follow eval-make-fast-2.
(define EVAL-FAST-OPERATOR-NAMES '("+" "-" "*" "=" "<" ">" "<=" ">="))

(define (eval-operator-opcode name names index)
  (if (string=? name (car names))
      index
      (eval-operator-opcode name (cdr names) (+ index 1))))

(define (eval-native-value suffix definitions)
  (cond
    ((null? definitions) #f)
    ((string=? suffix (car (car definitions))) (cdr (car definitions)))
    (else (eval-native-value suffix (cdr definitions)))))

(define (eval-fast-operators aliases definitions)
  (if (null? aliases)
      '()
      (let ((value (eval-native-value (car (car aliases)) definitions)))
        (if value
            (cons (cons value (eval-operator-opcode
                                (cdr (car aliases)) EVAL-FAST-OPERATOR-NAMES 0))
                  (eval-fast-operators (cdr aliases) definitions))
            (eval-fast-operators (cdr aliases) definitions)))))

(set-car! EVAL-FAST-OPERATORS
          (eval-fast-operators (frontend-numeric-alias-suffixes)
                               (eval-native-definitions "scheme/base")))

; Pairs (name . emitted-name) are in the order of the native entry.
(define (eval-bind-native! environment pairs natives)
  (if (pair? pairs)
      (begin
        (set-car! (eval-environment-cell environment (cdr (car pairs)))
                  (cdr (car natives)))
        (eval-bind-native! environment (cdr pairs) (cdr natives)))
      EVAL-UNSPECIFIED))

; The first-class values of the unbounded inline operators accept a bounded
; number of operands (PRIMITIVE-VALUE-SPREAD); longer calls fold through
; the same operator, so each step has native semantics.
(define EVAL-SPREAD-LIMIT 6)

(define (eval-fold operator value operands)
  (if (null? operands)
      value
      (eval-fold operator (operator value (car operands)) (cdr operands))))

(define (eval-chain? compare values)
  (cond
    ((null? (cdr values)) #t)
    ((compare (car values) (car (cdr values)))
     (eval-chain? compare (cdr values)))
    (else #f)))

(define (eval-arithmetic native required)
  (lambda operands
    (if (> (length operands) (+ required EVAL-SPREAD-LIMIT))
        (eval-fold native (car operands) (cdr operands))
        (apply native operands))))

(define (eval-comparison native)
  (lambda operands
    (if (> (length operands) (+ 2 EVAL-SPREAD-LIMIT))
        (eval-chain? native operands)
        (apply native operands))))

(define (eval-primitive-binding binding)
  (let ((name (car binding)) (value (cdr binding)))
    (cond
      ((string=? name "+") (cons name (eval-arithmetic value 0)))
      ((string=? name "*") (cons name (eval-arithmetic value 0)))
      ((string=? name "-") (cons name (eval-arithmetic value 1)))
      ((primitive-comparison-name? name) (cons name (eval-comparison value)))
      (else binding))))

(define (primitive-comparison-name? name)
  (cond
    ((string=? name "<") #t)
    ((string=? name ">") #t)
    ((string=? name "=") #t)
    ((string=? name "<=") #t)
    (else (string=? name ">="))))

(define EVAL-PRIMITIVE-BINDINGS
  (cons (cons "list" (lambda values values))
        (map eval-primitive-binding (eval-native-definitions "npl/primitive"))))

(define (set-eval-standard-root! root)
  (frontend-set-standard-root! root))

; (npl eval-runtime) inside npli: (scheme eval) and (scheme load) reach the
; evaluator through these names, bound per environment.
(define EVAL-RUNTIME-LIBRARY-EXPORTS
  (list
    (cons "make-environment" "%eval-make-environment")
    (cons "evaluate" "%eval-evaluate")
    (cons "load-file" "%eval-load-file")))

(define (eval-runtime-bindings environment)
  (list
    (cons "%eval-make-environment" make-eval-environment-from-imports)
    (cons "%eval-evaluate" eval-in-environment)
    (cons "%eval-load-file"
          (lambda (filename) (eval-load-file filename environment)))
    (cons "%eval-bind-native"
          (lambda (key pairs)
            (eval-bind-native! environment pairs
                               (eval-native-definitions key))))))

(define (eval-new-environment)
  (let ((session (make-frontend-session)))
    (frontend-register-runtime-library!
      session "npl/eval-runtime" EVAL-RUNTIME-LIBRARY-EXPORTS)
    (let ((environment
            (make-eval-environment-record (eval-make-table) #t session)))
      (eval-install-bindings! environment EVAL-PRIMITIVE-BINDINGS)
      (eval-install-bindings! environment (eval-runtime-bindings environment))
      environment)))

; The interaction environment: primitives and core syntax without imports.
(define (make-eval-environment) (eval-new-environment))

; (environment import-set ...): only the imported names, then frozen.
(define (make-eval-environment-from-imports import-sets)
  (if (eval-proper-list? import-sets)
      (eval-import-environment import-sets)
      (error "environment: expected import sets")))

(define (eval-import-environment import-sets)
  (let ((environment (eval-new-environment)))
    (frontend-reset-interaction-bindings!
      (eval-environment-frontend-session environment))
    (eval-in-environment (cons 'import import-sets) environment)
    (frontend-require-bound! (eval-environment-frontend-session environment))
    (eval-environment-set-mutable! environment #f)
    environment))

(define (eval-definition-form? expression)
  (if (pair? expression)
      (memq (car expression) '(define define-syntax define-values
                               define-record-type))
      #f))

; Every value of the last form is returned (it runs in tail position).
(define (eval-run-forms forms environment)
  (cond
    ((null? forms) EVAL-UNSPECIFIED)
    ((null? (cdr forms)) ((eval-compile (car forms) '() environment) #f))
    (else
     ((eval-compile (car forms) '() environment) #f)
     (eval-run-forms (cdr forms) environment))))

(define (eval-expanded forms environment)
  (if (frontend-error? forms)
      (error (frontend-error-message forms))
      (eval-run-forms forms environment)))

(define (eval-checked-expression expression environment)
  (if (eval-environment-mutable? environment)
      (eval-expanded (eval-expand expression environment) environment)
      (eval-frozen-expression expression environment)))

(define (eval-frozen-expression expression environment)
  (if (eval-definition-form? expression)
      (error "eval: immutable environment")
      (eval-expanded (eval-expand expression environment) environment)))

(define (eval-in-environment expression environment)
  (if (eval-environment? environment)
      (eval-checked-expression expression environment)
      (error "eval: expected an environment")))

(define (eval-load-forms port environment)
  (let ((form (read port)))
    (if (eof-object? form)
        (begin (close-input-port port) EVAL-UNSPECIFIED)
        (begin
          (eval-in-environment form environment)
          (eval-load-forms port environment)))))

(define (eval-load-file filename environment)
  (eval-load-forms (open-input-file filename) environment))

; Text of an uncaught raise, as compiled programs report it
; (npl_report_error_object): the message, then ": " and each irritant.
(define (eval-irritants-text irritants port)
  (if (pair? irritants)
      (begin
        (display ": " port)
        (write (car irritants) port)
        (eval-irritants-text (cdr irritants) port))
      (get-output-string port)))

(define (eval-condition-text condition)
  (if (error-object? condition)
      (let ((port (open-output-string)))
        (display (error-object-message condition) port)
        (eval-irritants-text (error-object-irritants condition) port))
      "uncaught exception"))
