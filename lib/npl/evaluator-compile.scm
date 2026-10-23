; Closure compiler for npli (docs/design-evaluator.md). Each core form of the
; shared frontend (tags in src/compiler/frontend/forms.npl) becomes, once, a
; native procedure of the run-time frame. Locals are lexical addresses into
; frames #(parent value ...); globals are cells of the environment's table.
; Procedures, control operators, primitives, and errors are the native ones,
; so evaluated code and compiled code call each other freely.
;
; Control conversion copies an if's continuation into both branches, so the
; code here avoids compound tests and non-tail one-armed ifs: either would
; duplicate the rest of the procedure (see src/compiler/frontend/
; c-primitives.npl).

(define EVAL-UNSPECIFIED (if #f #f))
(define (eval-unspecified? value) (eq? value EVAL-UNSPECIFIED))

; ---------------- global cells ----------------

; A cell is (value . name). An unbound cell holds EVAL-UNBOUND, so code
; compiled before a definition (an earlier REPL form) sees the definition.
(define EVAL-UNBOUND (list 'unbound))
(define EVAL-TABLE-SIZE 509)

(define (eval-make-table) (make-vector EVAL-TABLE-SIZE '()))

(define (eval-name-hash name index hash)
  (if (= index (string-length name))
      hash
      (eval-name-hash
        name (+ index 1)
        (remainder (+ (* hash 31) (char->integer (string-ref name index)))
                   EVAL-TABLE-SIZE))))

(define (eval-bucket-cell name cells)
  (cond
    ((null? cells) #f)
    ((string=? name (cdr (car cells))) (car cells))
    (else (eval-bucket-cell name (cdr cells)))))

(define (eval-new-cell table bucket name)
  (let ((cell (cons EVAL-UNBOUND name)))
    (vector-set! table bucket (cons cell (vector-ref table bucket)))
    cell))

(define (eval-table-cell table name)
  (let ((bucket (eval-name-hash name 0 (string-length name))))
    (let ((found (eval-bucket-cell name (vector-ref table bucket))))
      (if found found (eval-new-cell table bucket name)))))

(define (eval-unbound cell)
  (error (string-append "unbound identifier: " (cdr cell))))

; ---------------- environments ----------------

(define-record-type eval-environment
  (make-eval-environment-record table mutable session)
  eval-environment?
  (table eval-environment-table)
  (mutable eval-environment-mutable? eval-environment-set-mutable!)
  (session eval-environment-frontend-session))

(define (eval-environment-cell environment name)
  (eval-table-cell (eval-environment-table environment) name))

(define (eval-install-bindings! environment bindings)
  (for-each
    (lambda (binding)
      (set-car! (eval-environment-cell environment (car binding))
                (cdr binding)))
    bindings))

; ---------------- scopes ----------------

; A scope is one list of names per frame, innermost first. Slot 0 of a
; run-time frame is its parent, so the first name is at index 1.
(define (eval-scope-index name names index)
  (cond
    ((null? names) #f)
    ((string=? name (car names)) index)
    (else (eval-scope-index name (cdr names) (+ index 1)))))

(define (eval-scope-lookup name scope depth)
  (if (null? scope)
      #f
      (eval-scope-found name scope depth
                        (eval-scope-index name (car scope) 1))))

(define (eval-scope-found name scope depth index)
  (if index
      (cons depth index)
      (eval-scope-lookup name (cdr scope) (+ depth 1))))

(define (eval-frame-up frame depth)
  (if (= depth 0) frame (eval-frame-up (vector-ref frame 0) (- depth 1))))

(define (eval-core-tagged? value tag)
  (if (pair? value) (eqv? (car value) tag) #f))

(define (eval-core-symbol? value) (eval-core-tagged? value 100))

; ---------------- variables and definitions ----------------

(define (eval-local-reference depth index)
  (cond
    ((= depth 0) (lambda (frame) (vector-ref frame index)))
    ((= depth 1) (lambda (frame) (vector-ref (vector-ref frame 0) index)))
    ((= depth 2)
     (lambda (frame) (vector-ref (vector-ref (vector-ref frame 0) 0) index)))
    (else (lambda (frame) (vector-ref (eval-frame-up frame depth) index)))))

(define (eval-global-reference cell)
  (lambda (frame)
    (let ((value (car cell)))
      (if (eq? value EVAL-UNBOUND) (eval-unbound cell) value))))

(define (eval-compile-reference name scope environment)
  (let ((address (eval-scope-lookup name scope 0)))
    (if address
        (eval-local-reference (car address) (cdr address))
        (eval-global-reference (eval-environment-cell environment name)))))

(define (eval-global-assignment cell value)
  (lambda (frame)
    (let ((new-value (value frame)))
      (if (eq? (car cell) EVAL-UNBOUND)
          (eval-unbound cell)
          (set-car! cell new-value)))))

(define (eval-compile-set form scope environment)
  (let ((name (cdr (car (cdr form))))
        (value (eval-compile (car (cdr (cdr form))) scope environment)))
    (let ((address (eval-scope-lookup name scope 0)))
      (cond
        (address
         (let ((depth (car address)) (index (cdr address)))
           (lambda (frame)
             (vector-set! (eval-frame-up frame depth) index (value frame)))))
        ((eval-environment-mutable? environment)
         (eval-global-assignment (eval-environment-cell environment name)
                                 value))
        (else (lambda (frame) (error "eval: immutable binding")))))))

(define (eval-global-definition cell value)
  (lambda (frame)
    (set-car! cell (value frame))))

; Definitions reach the compiler only at top level: the frontend turns
; internal ones into letrec* (core-body-expression).
(define (eval-compile-definition name value scope environment)
  (cond
    ((pair? scope) (error "eval: definition in expression context"))
    ((eval-environment-mutable? environment)
     (eval-global-definition (eval-environment-cell environment name) value))
    (else (lambda (frame) (error "eval: immutable environment")))))

(define (eval-compile-define form scope environment)
  (eval-compile-definition
    (cdr (car (cdr form)))
    (eval-compile (car (cdr (cdr form))) scope environment)
    scope environment))

(define (eval-compile-define-function form scope environment)
  (let ((signature (car (cdr form))))
    (eval-compile-definition
      (cdr (car signature))
      (eval-compile-lambda-parts (cdr signature) (car (cdr (cdr form)))
                                 scope environment)
      scope environment)))

; ---------------- procedures ----------------

(define (eval-fixed-formals formals)
  (if (pair? formals)
      (if (eval-core-symbol? formals)
          '()
          (cons (cdr (car formals)) (eval-fixed-formals (cdr formals))))
      '()))

(define (eval-rest-formal formals)
  (cond
    ((not (pair? formals)) #f)
    ((eval-core-symbol? formals) (cdr formals))
    (else (eval-rest-formal (cdr formals)))))

; The error object compiled code raises for a wrong argument count.
(define (eval-arity-error count)
  (error "procedure: wrong number of arguments" count))

; Frame #(parent fixed ... rest) for a procedure with more fixed parameters
; than eval-make-procedure spells out.
(define (eval-rest-list arguments count)
  (if (= count 0)
      (list arguments)
      (cons (car arguments) (eval-rest-list (cdr arguments) (- count 1)))))

(define (eval-arguments-accepted? count fixed rest)
  (if rest (>= count fixed) (= count fixed)))

(define (eval-spread-frame frame arguments fixed rest)
  (let ((count (length arguments)))
    (if (eval-arguments-accepted? count fixed rest)
        (list->vector
          (cons frame (if rest (eval-rest-list arguments fixed) arguments)))
        (eval-arity-error count))))

; Small arities become native procedures of the same shape, so the native
; arity check and its error apply unchanged.
(define (eval-make-procedure fixed rest code)
  (if rest
      (cond
        ((= fixed 0)
         (lambda (frame) (lambda arguments (code (vector frame arguments)))))
        ((= fixed 1)
         (lambda (frame)
           (lambda (a . arguments) (code (vector frame a arguments)))))
        ((= fixed 2)
         (lambda (frame)
           (lambda (a b . arguments) (code (vector frame a b arguments)))))
        (else (eval-make-spread-procedure fixed rest code)))
      (cond
        ((= fixed 0) (lambda (frame) (lambda () (code (vector frame)))))
        ((= fixed 1) (lambda (frame) (lambda (a) (code (vector frame a)))))
        ((= fixed 2)
         (lambda (frame) (lambda (a b) (code (vector frame a b)))))
        ((= fixed 3)
         (lambda (frame) (lambda (a b c) (code (vector frame a b c)))))
        ((= fixed 4)
         (lambda (frame)
           (lambda (a b c d) (code (vector frame a b c d)))))
        (else (eval-make-spread-procedure fixed rest code)))))

(define (eval-make-spread-procedure fixed rest code)
  (lambda (frame)
    (lambda arguments
      (code (eval-spread-frame frame arguments fixed rest)))))

(define (eval-compile-lambda-parts formals body scope environment)
  (let ((fixed (eval-fixed-formals formals))
        (rest (eval-rest-formal formals)))
    (eval-make-procedure
      (length fixed) rest
      (eval-compile-body
        body
        (cons (if rest (append fixed (list rest)) fixed) scope)
        environment))))

(define (eval-compile-lambda form scope environment)
  (eval-compile-lambda-parts (car (cdr form)) (car (cdr (cdr form)))
                             scope environment))

(define (eval-compile-body body scope environment)
  (eval-compile (frontend-core-body-expression body) scope environment))

; ---------------- applications ----------------

(define (eval-run-list codes frame)
  (if (null? codes)
      '()
      (let ((value ((car codes) frame)))
        (cons value (eval-run-list (cdr codes) frame)))))

(define (eval-proper-list? value)
  (cond
    ((null? value) #t)
    ((pair? value) (eval-proper-list? (cdr value)))
    (else #f)))

(define (eval-compile-application expression scope environment)
  (if (eval-proper-list? expression)
      (eval-make-application
        (eval-compile (car expression) scope environment)
        (eval-compile-list (cdr expression) scope environment))
      (error "malformed application")))

; The operator is evaluated first, then the operands from left to right.
(define (eval-make-application operator operands)
  (let ((count (length operands)))
    (cond
      ((= count 0) (lambda (frame) ((operator frame))))
      ((= count 1)
       (let ((a (car operands)))
         (lambda (frame)
           (let ((procedure (operator frame)))
             (procedure (a frame))))))
      ((= count 2)
       (let ((a (car operands)) (b (car (cdr operands))))
         (lambda (frame)
           (let ((procedure (operator frame)))
             (let ((x (a frame)))
               (procedure x (b frame)))))))
      ((= count 3)
       (let ((a (car operands)) (b (car (cdr operands)))
             (c (car (cdr (cdr operands)))))
         (lambda (frame)
           (let ((procedure (operator frame)))
             (let ((x (a frame)))
               (let ((y (b frame)))
                 (procedure x y (c frame))))))))
      (else
       (lambda (frame)
         (let ((procedure (operator frame)))
           (apply procedure (eval-run-list operands frame))))))))

(define (eval-compile-list expressions scope environment)
  (if (null? expressions)
      '()
      (cons (eval-compile (car expressions) scope environment)
            (eval-compile-list (cdr expressions) scope environment))))

; ---------------- conditionals, sequences, and bindings ----------------

(define (eval-compile-if form scope environment)
  (let ((test (eval-compile (car (cdr form)) scope environment))
        (consequent (eval-compile (car (cdr (cdr form))) scope environment))
        (alternate
          (if (null? (cdr (cdr (cdr form))))
              (lambda (frame) EVAL-UNSPECIFIED)
              (eval-compile (car (cdr (cdr (cdr form)))) scope environment))))
    (lambda (frame)
      (if (test frame) (consequent frame) (alternate frame)))))

(define (eval-run-sequence codes frame)
  (if (null? (cdr codes))
      ((car codes) frame)
      (begin
        ((car codes) frame)
        (eval-run-sequence (cdr codes) frame))))

(define (eval-make-sequence codes)
  (cond
    ((null? codes) (lambda (frame) EVAL-UNSPECIFIED))
    ((null? (cdr codes)) (car codes))
    ((null? (cdr (cdr codes)))
     (let ((first (car codes)) (second (car (cdr codes))))
       (lambda (frame) (first frame) (second frame))))
    (else (lambda (frame) (eval-run-sequence codes frame)))))

(define (eval-binding-names bindings)
  (if (null? bindings)
      '()
      (cons (cdr (car (car bindings)))
            (eval-binding-names (cdr bindings)))))

(define (eval-binding-inits bindings)
  (if (null? bindings)
      '()
      (cons (cdr (car bindings)) (eval-binding-inits (cdr bindings)))))

(define (eval-make-let inits code)
  (let ((count (length inits)))
    (cond
      ((= count 0) (lambda (frame) (code (vector frame))))
      ((= count 1)
       (let ((a (car inits)))
         (lambda (frame) (code (vector frame (a frame))))))
      ((= count 2)
       (let ((a (car inits)) (b (car (cdr inits))))
         (lambda (frame)
           (let ((x (a frame)))
             (code (vector frame x (b frame)))))))
      (else
       (lambda (frame)
         (code (list->vector (cons frame (eval-run-list inits frame)))))))))

(define (eval-compile-let form scope environment)
  (let ((bindings (car (cdr form))))
    (eval-make-let
      (eval-compile-list (eval-binding-inits bindings) scope environment)
      (eval-compile-body (car (cdr (cdr form)))
                         (cons (eval-binding-names bindings) scope)
                         environment))))

(define (eval-store-sequential! inits frame index)
  (if (pair? inits)
      (begin
        (vector-set! frame index ((car inits) frame))
        (eval-store-sequential! (cdr inits) frame (+ index 1)))
      frame))

(define (eval-store-values! values frame index)
  (if (pair? values)
      (begin
        (vector-set! frame index (car values))
        (eval-store-values! (cdr values) frame (+ index 1)))
      frame))

(define (eval-letrec-frame frame count)
  (let ((inner (make-vector (+ count 1) EVAL-UNSPECIFIED)))
    (vector-set! inner 0 frame)
    inner))

; letrec evaluates every init before assigning any; letrec* assigns each in
; turn. With one binding (named let) they agree.
(define (eval-make-letrec inits code sequential)
  (let ((count (length inits)))
    (cond
      ((= count 1)
       (let ((a (car inits)))
         (lambda (frame)
           (let ((inner (vector frame EVAL-UNSPECIFIED)))
             (vector-set! inner 1 (a inner))
             (code inner)))))
      (sequential
       (lambda (frame)
         (code (eval-store-sequential! inits (eval-letrec-frame frame count)
                                       1))))
      (else
       (lambda (frame)
         (let ((inner (eval-letrec-frame frame count)))
           (code (eval-store-values! (eval-run-list inits inner)
                                     inner 1))))))))

(define (eval-compile-letrec form scope environment sequential)
  (let ((bindings (car (cdr form))))
    (let ((inner (cons (eval-binding-names bindings) scope)))
      (eval-make-letrec
        (eval-compile-list (eval-binding-inits bindings) inner environment)
        (eval-compile-body (car (cdr (cdr form))) inner environment)
        sequential))))

; ---------------- dispatch ----------------

(define (eval-constant value) (lambda (frame) value))

(define (eval-compile-tagged tag form scope environment)
  (cond
    ((= tag 101) (eval-constant (cdr form)))
    ((= tag 102) (eval-constant (integer->char (cdr form))))
    ((= tag 103)
     (eval-constant
       (list->vector (frontend-datum->runtime (car (cdr form))))))
    ((= tag 104)
     (eval-constant (frontend-bytevector-items->runtime (car (cdr form)))))
    ((= tag 108) (eval-constant #t))
    ((= tag 109) (eval-constant #f))
    ((= tag 110) (eval-constant EVAL-UNSPECIFIED))
    ((= tag 204) (eval-constant (frontend-datum->runtime (car (cdr form)))))
    ((= tag 200) (eval-compile-define form scope environment))
    ((= tag 211) (eval-compile-define-function form scope environment))
    ((= tag 212) (eval-compile-lambda form scope environment))
    ((= tag 213) (eval-compile-if form scope environment))
    ((= tag 214) (eval-compile-let form scope environment))
    ((= tag 216)
     (eval-make-sequence (eval-compile-list (cdr form) scope environment)))
    ((= tag 217) (eval-compile-set form scope environment))
    ((= tag 220)
     (eval-compile
       (frontend-expand-core-letstar (car (cdr form)) (car (cdr (cdr form))))
       scope environment))
    ((= tag 221) (eval-compile-letrec form scope environment #f))
    ((= tag 222) (eval-compile-letrec form scope environment #t))
    (else (error "unsupported core expression"))))

(define (eval-compile expression scope environment)
  (cond
    ((eval-core-symbol? expression)
     (eval-compile-reference (cdr expression) scope environment))
    ((not (pair? expression)) (eval-constant expression))
    ((number? (car expression))
     (eval-compile-tagged (car expression) expression scope environment))
    (else (eval-compile-application expression scope environment))))
