;;;; Map, fold, and unfold

;; If maybe is a Just, apply proc to its payload and wrap the result
;; in a Just.  Otherwise, return Nothing.
(define (maybe-map proc maybe)
  (assume (procedure? proc))
  (assume (maybe? maybe))
  (if (nothing? maybe)
      nothing-obj
      (call-with-values (lambda () (fast-apply proc (just-objs maybe)))
                        just)))

(define (maybe-for-each proc maybe)
  (assume (procedure? proc))
  (maybe-ref maybe (const #f) proc)
  unspecified)

;; If maybe is a Just, apply kons to its payload values and nil.
;; Otherwise, return nil.
(define (maybe-fold kons nil maybe)
  (assume (procedure? kons))
  (assume (maybe? maybe))
  (if (nothing? maybe)
      nil
      (let ((objs (just-objs maybe)))
        (if (singleton? objs)
            (kons (car objs) nil)
            (apply kons (append objs (list nil)))))))

;; If the seeds satisfy stop?, return Nothing.  Otherwise, call
;; successor on seeds and apply stop? to the results; if stop? returns
;; true, apply mapper to seeds and return the results wrapped in a Just.
(define (maybe-unfold stop? mapper successor . seeds)
  (assume (procedure? stop?))
  (assume (procedure? mapper))
  (if (singleton? seeds)
      (let ((seed (car seeds)))  ; fast path
        (if (stop? seed)
            nothing-obj
            (begin
             ;; successor might return multiple seeds.
             (assume (call-with-values (lambda () (successor seed)) stop?))
             (call-with-values (lambda () (mapper (car seeds))) just))))
      (if (apply stop? seeds)
          nothing-obj
          (begin
           (assume (call-with-values (lambda () (apply successor seeds))
                                     stop?))
           (call-with-values (lambda () (apply mapper seeds)) just)))))

;; If either is a Right, apply proc to its payload and wrap the result
;; in a Right.  Otherwise, return either.
(define (either-map proc either)
  (assume (procedure? proc))
  (assume (either? either))
  (if (left? either)
      either
      (call-with-values (lambda () (fast-apply proc (right-objs either)))
                        right)))

(define (either-for-each proc either)
  (assume (procedure? proc))
  (either-ref either (const #f) proc)
  unspecified)

;; If either is a Right, apply kons to its payload values and nil.
;; Otherwise, return nil.
(define (either-fold kons nil either)
  (assume (procedure? kons))
  (assume (either? either))
  (if (left? either)
      nil
      (let ((objs (right-objs either)))
        (if (singleton? objs)
            (kons (car objs) nil)
            (apply kons (append objs (list nil)))))))

;; If the seeds satisfy stop?, return a Left of seeds.  Otherwise, call
;; successor on seeds and apply stop? to the results; if stop? returns
;; true, apply mapper to seeds and return the results wrapped in a Right.
(define (either-unfold stop? mapper successor . seeds)
  (assume (procedure? stop?))
  (assume (procedure? mapper))
  (if (singleton? seeds)
      (let ((seed (car seeds)))  ; fast path
        (if (stop? seed)
            (raw-left seeds)
            (begin
             ;; successor might return multiple values.
             (assume (call-with-values (lambda () (successor seed)) stop?))
             (call-with-values (lambda () (apply mapper seeds)) right))))
      (if (apply stop? seeds)
          (raw-left seeds)
          (begin
           (assume (call-with-values (lambda () (apply successor seeds))
                                     stop?))
           (call-with-values (lambda () (apply mapper seeds)) right)))))


;;;; Syntax

(define-syntax maybe-if
  (syntax-rules ()
    ((_ maybe-expr just-expr nothing-expr)
     (let ((mval maybe-expr))
       (unless (maybe? mval)
         (error "maybe-if: ill-typed value"))
       (if (just? mval) just-expr nothing-expr)))))

;; Return the value of expr if it satisfies pred.
(define-syntax %guard-value
  (syntax-rules ()
    ((_ pred expr)
     (let ((val expr))
       (unless (pred val)
         (error "ill-typed value" pred val))
       val))))

;; Maybe analog of and.  Evaluate the argument expressions in order.
;; If any expression evaluates to Nothing, return it.  Otherwise,
;; return the last Just.
(define-syntax maybe-and
  (syntax-rules ()
    ((_) (just unspecified))
    ((_ maybe-expr) (%guard-value maybe? maybe-expr))
    ((_ maybe-expr maybe-expr* ...)
     (let ((maybe maybe-expr))
       (cond ((just? maybe) (maybe-and maybe-expr* ...))
             ((nothing? maybe) nothing-obj)
             (else
              (error "maybe-and: ill-typed value" maybe? maybe)))))))

;; Maybe analog of or.  Evaluate the argument expressions in order.
;; If any expression evaluates to a Just, return it immediately.
;; Otherwise, return Nothing.
(define-syntax maybe-or
  (syntax-rules ()
    ((_) (nothing))
    ((_ maybe-expr) (%guard-value maybe? maybe-expr))
    ((_ maybe-expr maybe-expr* ...)
     (let ((maybe maybe-expr))
       (cond ((just? maybe) maybe)
             ((nothing? maybe) (maybe-or maybe-expr* ...))
             (else
              (error "maybe-or: ill-typed value" maybe? maybe)))))))

;; Maybe analog of SRFI 2's and-let*.  Each claw evaluates an expression
;; or bound variable to a Maybe, or binds the payload of the value of a
;; Maybe expression to a name in later claws and the body.  If any claw
;; gives a Nothing, the whole expression evaluates to Nothing.
(define-syntax maybe-let*
  (syntax-rules ()
    ((_ () expr1 expr2 ...)
     (call-with-values (lambda () expr1 expr2 ...) just))
    ((_ ((id maybe-expr) . claws) . body)
     (let ((maybe maybe-expr))
       (cond ((and (just? maybe) (singleton? (just-objs maybe)))
              (let ((id (car (just-objs maybe))))
                (maybe-let* claws . body)))
             ((nothing? maybe) nothing-obj)
             (else (error "ill-typed value" maybe? maybe)))))
    ((_ ((maybe-expr) . claws) . body)
     (let ((maybe maybe-expr))
       (cond ((just? maybe) (maybe-let* claws . body))
             ((nothing? maybe) nothing-obj)
             (else (error "ill-typed value" maybe? maybe)))))
    ((_ (id . claws) . body)
     (cond ((just? id) (maybe-let* claws . body))
           ((nothing? id) nothing-obj)
           (else (error "ill-typed value" maybe? id))))
    ((_ . _)
     (syntax-error "ill-formed maybe-let* form"))))

;; Like maybe-let*, but a claw of the form (<formals> <maybe-expr>)
;; binds the payload of the value of <maybe-expr> to <formals> in the
;; manner of let-values.
(define-syntax maybe-let*-values
  (syntax-rules ()
    ((_ () expr1 expr2 ...)
     (call-with-values (lambda () expr1 expr2 ...) just))
    ((_ (((id id* ...) maybe-expr) . claws) . body)
     (maybe-bind (%guard-value maybe? maybe-expr)
                 (lambda (id id* ...)
                   (maybe-let*-values claws . body))))
    ((_ ((ids maybe-expr) . claws) . body)
     (maybe-bind (%guard-value maybe? maybe-expr)
                 (lambda ids
                   (maybe-let*-values claws . body))))
    ((_ ((maybe-expr) . claws) . body)
     (let ((maybe maybe-expr))
       (cond ((just? maybe) (maybe-let*-values claws . body))
             ((nothing? maybe) nothing-obj)
             (else (error "ill-typed value" maybe? maybe)))))
    ((_ (id . claws) . body)
     (cond ((just? id) (maybe-let*-values claws . body))
           ((nothing? id) nothing-obj)
           (else (error "ill-typed value" maybe? id))))
    ((_ . _)
     (syntax-error "ill-formed maybe-let*-values form"))))

;; Either analog of and.  Evaluate the argument expressions in order.
;; If any expression evaluates to a Left, return it immediately.
;; Otherwise, return the last Right.
(define-syntax either-and
  (syntax-rules ()
    ((_) (right unspecified))
    ((_ either-expr) (%guard-value either? either-expr))
    ((_ either-expr either-expr* ...)
     (let ((either either-expr))
       (cond ((right? either) (either-and either-expr* ...))
             ((left? either) either)
             (else (error "ill-typed value" either? either)))))))

;; Either analog of or.  Evaluate the argument expressions in order.
;; If any expression evaluates to a Right, return it immediately.
;; Otherwise, return the last Left.
(define-syntax either-or
  (syntax-rules ()
    ((_) (left unspecified))
    ((_ either-expr) (%guard-value either? either-expr))
    ((_ either-expr either-expr* ...)
     (let ((either either-expr))
       (cond ((right? either) either)
             ((left? either) (either-or either-expr* ...))
             (else (error "ill-typed value" either? either)))))))

;; Either analog of SRFI 2's and-let*.  Each claw evaluates an expression
;; or bound variable to a Maybe, or binds the payload of the value of an
;; Either expression to a name in later claws and the body.  If any claw
;; gives a Left, then the whole expression evaluates to that Left.
(define-syntax either-let*
  (syntax-rules ()
    ((_ () expr1 expr2 ...)
     (call-with-values (lambda () expr1 expr2 ...) right))
    ((_ ((id either-expr) . claws) . body)
     (let ((either either-expr))
       (cond ((and (right? either) (singleton? (right-objs either)))
              (let ((id (car (right-objs either))))
                (either-let* claws . body)))
             ((left? either) either)
             (else (error "ill-typed value" either? either)))))
    ((_ ((either-expr) . claws) . body)
     (let ((either either-expr))
       (cond ((right? either) (either-let* claws . body))
             ((left? either) either)
             (else (error "ill-typed value" either? either)))))
    ((_ (id . claws) . body)
     (cond ((right? id) (either-let* claws . body))
           ((left? id) id)
           (else (error "ill-typed value" either? id))))
    ((_ . _)
     (syntax-error "ill-formed either-let* form"))))

;; Like either-let*, but a claw of the form (<formals> <either-expr>)
;; binds the payload of the value of <either-expr> to <formals> in the
;; manner of let-values.
(define-syntax either-let*-values
  (syntax-rules ()
    ((_ () expr1 expr2 ...)
     (call-with-values (lambda () expr1 expr2 ...) right))
    ((_ (((id id* ...) either-expr) . claws) . body)
     (either-bind (%guard-value either? either-expr)
                  (lambda (id id* ...)
                    (either-let*-values claws . body))))
    ((_ ((ids either-expr) . claws) . body)
     (either-bind (%guard-value either? either-expr)
                  (lambda ids
                    (either-let*-values claws . body))))
    ((_ ((either-expr) . claws) . body)
     (let ((either either-expr))
       (cond ((right? either) (either-let*-values claws . body))
             ((left? either) either)
             (else (error "ill-typed value" either? either)))))
    ((_ (id . claws) . body)
     (cond ((right? id) (either-let*-values claws . body))
           ((left? id) id)
           (else (error "ill-typed value" either? id))))
    ((_ . _)
     (syntax-error "ill-formed either-let*-values form"))))

(define-syntax either-guard
  (syntax-rules ()
    ((_ pred-expr expr1 expr2 ...)
     (exception->either pred-expr (lambda () expr1 expr2 ...)))))


;;;; Trivalent logic

;;; In the following procedures, (just #f) is considered to be false.
;;; All other Just values are taken to be true.

(define (just->boolean maybe)
  (not (equal? (just-objs maybe) '(#f))))

(define (tri-not maybe)
  (maybe-bind maybe (lambda (x) (just (not x)))))

;; Returns #t if all arguments are true or all false.  If any argument
;; is Nothing or if any two arguments have different (tri-)truth values,
;; #f is returned.
(define (tri=? maybe . ms)
  (let ((make-pred
         (lambda (b)
           (lambda (m)
             (assume (maybe? m))
             (and (just? m) (eqv? (just->boolean m) b))))))
    (if (nothing? maybe)
        (just #f)
        (let ((tri-same? (make-pred (just->boolean maybe))))
          (if (every tri-same? ms) (just #t) (just #f))))))

;; Returns #t if all arguments are true.  If any argument is false or
;; Nothing, return the first such object.
(define (tri-and . maybes)
  (or (find (lambda (m)
              (assume (maybe? m))
              (or (nothing? m) (not (just->boolean m))))
            maybes)
      (just #t)))

;; Returns #f if all arguments are false.  If any argument is true or
;; Nothing, return the first such object.
(define (tri-or . maybes)
  (or (find (lambda (m)
              (assume (maybe? m))
              (or (nothing? m) (just->boolean m)))
            maybes)
      (just #f)))

;; If all arguments are Nothing, then return Nothing.  Otherwise,
;; return the first Just value.
(define (tri-merge . maybes)
  (or (find just? maybes) nothing-obj))
