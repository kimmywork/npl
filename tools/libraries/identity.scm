(begin
    ; ---- definition identity ----------------------------------------------
    ; Two definitions are "the same" when their normalized forms are equal:
    ; bound variables are renamed by binding order (alpha-equivalence), the
    ; definition's own name becomes %self, and the derived forms and numeric
    ; comparisons with an obvious canonical spelling are rewritten:
    ;
    ;   (and a b)          -> (if a b #f)
    ;   (when t b ...)     -> (if t (begin b ...))
    ;   (unless t b ...)   -> (if (not t) (begin b ...))
    ;   (cond ...)         -> nested if
    ;   (not (negative? x)), (<= 0 x)  -> (>= x 0)
    ;
    ; This is a conservative approximation of semantic equality: equal
    ; normalized forms always compute the same thing; different forms may
    ; still be equivalent and are not reported.

    (define (identity-fresh counter)
      (vector-set! counter 0 (+ (vector-ref counter 0) 1))
      (string->symbol
        (string-append "%v" (number->string (vector-ref counter 0)))))

    (define (identity-lookup symbol env)
      (let ((entry (assq symbol env)))
        (if entry (cdr entry) symbol)))

    (define (identity-bound? symbol env)
      (if (assq symbol env) #t #f))

    ; Bind every variable of a lambda list (proper, dotted, or a symbol).
    (define (identity-bind-formals formals env counter)
      (cond
        ((symbol? formals)
         (let ((fresh (identity-fresh counter)))
           (cons fresh (cons (cons formals fresh) env))))
        ((pair? formals)
         (let* ((head (if (symbol? (car formals))
                          (identity-fresh counter)
                          (car formals)))
                (env (if (symbol? (car formals))
                         (cons (cons (car formals) head) env)
                         env))
                (rest (identity-bind-formals (cdr formals) env counter)))
           (cons (cons head (car rest)) (cdr rest))))
        (else (cons formals env))))

    (define (identity-map items env counter)
      (cond
        ((pair? items)
         (let ((head (identity-normalize (car items) env counter)))
           (cons head (identity-map (cdr items) env counter))))
        ((null? items) '())
        (else (identity-normalize items env counter))))

    ; ---- derived forms ----

    (define (identity-and items)
      (cond
        ((null? items) #t)
        ((null? (cdr items)) (car items))
        (else (list 'if (car items) (identity-and (cdr items)) #f))))

    (define (identity-begin items)
      (if (if (pair? items) (null? (cdr items)) #f)
          (car items)
          (cons 'begin items)))

    (define (identity-cond clauses)
      (if (null? clauses)
          '(if #f #f)
          (let ((clause (car clauses)))
            (cond
              ((not (pair? clause)) (cons 'cond clauses))
              ((eq? (car clause) 'else) (identity-begin (cdr clause)))
              ((null? (cdr clause))
               (list 'or (car clause) (identity-cond (cdr clauses))))
              ((eq? (car (cdr clause)) '=>) (cons 'cond clauses))
              (else
               (list 'if (car clause) (identity-begin (cdr clause))
                     (identity-cond (cdr clauses))))))))

    (define (identity-desugar form)
      (let ((head (car form)) (rest (cdr form)))
        (cond
          ((eq? head 'and) (identity-and rest))
          ((if (eq? head 'when) (pair? rest) #f)
           (list 'if (car rest) (identity-begin (cdr rest))))
          ((if (eq? head 'unless) (pair? rest) #f)
           (list 'if (list 'not (car rest)) (identity-begin (cdr rest))))
          ((eq? head 'cond)
           (let ((desugared (identity-cond rest)))
             (if (if (pair? desugared) (eq? (car desugared) 'cond) #f)
                 #f
                 desugared)))
          (else #f))))

    ; ---- numeric comparison spelling ----

    (define (identity-flip op)
      (cond ((eq? op '<) '>) ((eq? op '>) '<)
            ((eq? op '<=) '>=) ((eq? op '>=) '<=)
            ((eq? op '=) '=) (else #f)))

    (define (identity-negate op)
      (cond ((eq? op '<) '>=) ((eq? op '>=) '<)
            ((eq? op '>) '<=) ((eq? op '<=) '>)
            (else #f)))

    (define (identity-compare? form)
      (if (pair? form)
          (if (identity-flip (car form))
              (if (pair? (cdr form))
                  (if (pair? (cdr (cdr form)))
                      (null? (cdr (cdr (cdr form))))
                      #f)
                  #f)
              #f)
          #f))

    (define (identity-canonical form)
      (cond
        ((not (pair? form)) form)
        ; (negative? x) (positive? x) (zero? x) -> comparisons with 0
        ((if (memq (car form) '(negative? positive? zero?))
             (if (pair? (cdr form)) (null? (cdr (cdr form))) #f)
             #f)
         (list (cond ((eq? (car form) 'negative?) '<)
                     ((eq? (car form) 'positive?) '>)
                     (else '=))
               (car (cdr form)) 0))
        ; literal on the left: (< 0 x) -> (> x 0)
        ((if (identity-compare? form)
             (if (number? (car (cdr form)))
                 (not (number? (car (cdr (cdr form)))))
                 #f)
             #f)
         (list (identity-flip (car form))
               (car (cdr (cdr form))) (car (cdr form))))
        ; (not (< a b)) -> (>= a b)
        ((if (eq? (car form) 'not)
             (if (pair? (cdr form))
                 (if (identity-compare? (car (cdr form)))
                     (identity-negate (car (car (cdr form))))
                     #f)
                 #f)
             #f)
         (let ((inner (car (cdr form))))
           (cons (identity-negate (car inner)) (cdr inner))))
        (else form)))

    ; ---- binding forms ----

    (define (identity-binding-names bindings)
      (map (lambda (binding) (if (pair? binding) (car binding) binding))
           bindings))

    (define (identity-binding-inits bindings)
      (map (lambda (binding)
             (if (if (pair? binding) (pair? (cdr binding)) #f)
                 (car (cdr binding))
                 #f))
           bindings))

    (define (identity-let form env counter)
      (if (symbol? (car (cdr form)))
          ; named let: the loop name is bound in the body only
          (let* ((name (car (cdr form)))
                 (bindings (car (cdr (cdr form))))
                 (inits (identity-map (identity-binding-inits bindings)
                                      env counter))
                 (loop-name (identity-fresh counter))
                 (env (cons (cons name loop-name) env))
                 (bound (identity-bind-formals
                          (identity-binding-names bindings) env counter)))
            (cons 'let
                  (cons loop-name
                        (cons (map list (car bound) inits)
                              (identity-body (cdr (cdr (cdr form)))
                                             (cdr bound) counter)))))
          (let* ((bindings (car (cdr form)))
                 (inits (identity-map (identity-binding-inits bindings)
                                      env counter))
                 (bound (identity-bind-formals
                          (identity-binding-names bindings) env counter)))
            (cons 'let
                  (cons (map list (car bound) inits)
                        (identity-body (cdr (cdr form)) (cdr bound) counter))))))

    ; let* binds one at a time; letrec/letrec* see every name in each init.
    (define (identity-let* form env counter)
      (let loop ((bindings (car (cdr form))) (env env) (out '()))
        (if (null? bindings)
            (cons 'let* (cons (reverse out)
                              (identity-body (cdr (cdr form)) env counter)))
            (let* ((binding (car bindings))
                   (init (if (if (pair? binding) (pair? (cdr binding)) #f)
                             (identity-normalize (car (cdr binding)) env counter)
                             #f))
                   (fresh (identity-fresh counter)))
              (loop (cdr bindings)
                    (cons (cons (if (pair? binding) (car binding) binding)
                                fresh)
                          env)
                    (cons (list fresh init) out))))))

    (define (identity-letrec form env counter)
      (let* ((bindings (car (cdr form)))
             (bound (identity-bind-formals
                      (identity-binding-names bindings) env counter))
             (inits (identity-map (identity-binding-inits bindings)
                                  (cdr bound) counter)))
        (cons 'letrec
              (cons (map list (car bound) inits)
                    (identity-body (cdr (cdr form)) (cdr bound) counter)))))

    ; (define (f . formals) body ...) -> (lambda formals body ...)
    (define (identity-define-value form)
      (let ((target (car (cdr form))))
        (if (pair? target)
            (cons 'lambda (cons (cdr target) (cdr (cdr form))))
            (if (pair? (cdr (cdr form))) (car (cdr (cdr form))) #f))))

    (define (identity-define-name form)
      (let ((target (car (cdr form))))
        (if (pair? target) (car target) target)))

    (define (identity-internal-define? form env)
      (if (pair? form)
          (if (eq? (car form) 'define)
              (if (not (identity-bound? 'define env))
                  (if (pair? (cdr form))
                      (symbol? (identity-define-name form))
                      #f)
                  #f)
              #f)
          #f))

    ; A body binds its internal defines before any of them is normalized.
    (define (identity-body forms env counter)
      (let loop ((rest forms) (env env))
        (if (if (pair? rest) (identity-internal-define? (car rest) env) #f)
            (loop (cdr rest)
                  (cons (cons (identity-define-name (car rest))
                              (identity-fresh counter))
                        env))
            (let walk ((items forms))
              (cond
                ((not (pair? items)) '())
                ((identity-internal-define? (car items) env)
                 (let* ((form (car items))
                        (name (identity-lookup (identity-define-name form) env))
                        (value (identity-normalize (identity-define-value form)
                                                   env counter)))
                   (cons (list 'define name value) (walk (cdr items)))))
                (else
                 (let ((head (identity-normalize (car items) env counter)))
                   (cons head (walk (cdr items))))))))))

    (define (identity-normalize form env counter)
      (cond
        ((symbol? form) (identity-lookup form env))
        ((not (pair? form)) form)
        ((if (symbol? (car form)) (identity-bound? (car form) env) #f)
         (identity-canonical (identity-map form env counter)))
        ((memq (car form) '(quote quasiquote syntax quasisyntax syntax-case
                             syntax-rules er-macro-transformer))
         form)
        ((if (eq? (car form) 'lambda) (pair? (cdr form)) #f)
         (let ((bound (identity-bind-formals (car (cdr form)) env counter)))
           (cons 'lambda
                 (cons (car bound)
                       (identity-body (cdr (cdr form)) (cdr bound) counter)))))
        ((if (eq? (car form) 'let) (pair? (cdr form)) #f)
         (identity-let form env counter))
        ((if (eq? (car form) 'let*) (pair? (cdr form)) #f)
         (identity-let* form env counter))
        ((if (memq (car form) '(letrec letrec*)) (pair? (cdr form)) #f)
         (identity-letrec form env counter))
        ((if (symbol? (car form)) (identity-desugar form) #f)
         (identity-normalize (identity-desugar form) env counter))
        ((if (eq? (car form) 'begin)
             (if (pair? (cdr form)) (null? (cdr (cdr form))) #f)
             #f)
         (identity-normalize (car (cdr form)) env counter))
        (else (identity-canonical (identity-map form env counter)))))

    ; The normalized definition, or #f for forms that never denote the same
    ; binding twice (records create distinct types; define-values is rare).
    ; Unusual shapes (macro templates outside syntax-case, malformed
    ; binding lists) are not compared rather than failing the scan.
    (define (identity-definition-form form)
      (guard (condition (#t #f))
        (identity-definition-form* form)))

    (define (identity-definition-form* form)
      (let ((counter (vector 0)))
        (cond
          ((not (pair? form)) #f)
          ((eq? (car form) 'define)
           (identity-normalize
             (identity-define-value form)
             (list (cons (identity-define-name form) '%self))
             counter))
          ((eq? (car form) 'define-syntax)
           (if (if (pair? (cdr form)) (pair? (cdr (cdr form))) #f)
               (identity-normalize (car (cdr (cdr form)))
                                   (list (cons (car (cdr form)) '%self))
                                   counter)
               #f))
          ; Same C entry point and signature: argument names do not matter.
          ((eq? (car form) 'define-c-primitive)
           (if (if (pair? (cdr form)) (pair? (car (cdr form))) #f)
               (cons 'define-c-primitive
                     (cons (map (lambda (argument)
                                  (if (if (pair? argument)
                                          (pair? (cdr argument))
                                          #f)
                                      (car (cdr argument))
                                      argument))
                                (cdr (car (cdr form))))
                           (cdr (cdr form))))
               #f))
          ((eq? (car form) 'define-c-function)
           (if (pair? (cdr form))
               (cons 'define-c-function (cdr (cdr form)))
               #f))
          (else #f))))

    ; The procedure a definition only forwards to, or #f.  A forwarder
    ; passes its formals through unchanged, in order:
    ;   (lambda (%v1 %v2) (g %v1 %v2))
    ;   (lambda (%v1 . %v2) (apply g %v1 %v2))
    ; so it has the same signature and effect as G under another name.
    (define (identity-forward-target normalized)
      (if (if (pair? normalized)
              (if (eq? (car normalized) 'lambda)
                  (if (pair? (cdr normalized))
                      (if (pair? (cdr (cdr normalized)))
                          (null? (cdr (cdr (cdr normalized))))
                          #f)
                      #f)
                  #f)
              #f)
          (let ((formals (car (cdr normalized)))
                (call (car (cdr (cdr normalized)))))
            (if (pair? call)
                (cond
                  ((if (list? formals)
                       (if (symbol? (car call))
                           (equal? (cdr call) formals)
                           #f)
                       #f)
                   (identity-free-target (car call)))
                  ((if (eq? (car call) 'apply)
                       (if (pair? (cdr call))
                           (if (symbol? (car (cdr call)))
                               (equal? (cdr (cdr call))
                                       (identity-spread formals))
                               #f)
                           #f)
                       #f)
                   (identity-free-target (car (cdr call))))
                  (else #f))
                #f))
          #f))

    ; (a b . c) -> (a b c), the argument list apply receives.
    (define (identity-spread formals)
      (cond
        ((pair? formals) (cons (car formals) (identity-spread (cdr formals))))
        ((null? formals) '())
        (else (list formals))))

    ; Normalized bound variables are %v<n>; %self is the definition itself.
    (define (identity-free-target symbol)
      (let ((text (symbol->string symbol)))
        (if (if (>= (string-length text) 2)
                (string=? (substring text 0 2) "%v")
                #f)
            #f
            (if (eq? symbol '%self) #f symbol))))

    (define (identity-form-size form)
      (cond
        ((pair? form) (+ 1 (identity-form-size (car form))
                         (identity-form-size (cdr form))))
        ((vector? form) (vector-length form))
        (else 0)))

    (define (identity-form-key form)
      (let ((port (open-output-string)))
        (write form port)
        (get-output-string port)))
)
