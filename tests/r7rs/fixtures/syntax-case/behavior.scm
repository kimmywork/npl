; Procedural syntax-case expansion scenarios.  The same body is compiled by
; nplc, loaded by npli, and used as the Guile (rnrs syntax-case) oracle.
(define (report name value) (write (list name value)) (newline))

; --- clause selection, literals, ellipsis, tail patterns, vectors ---
(define-syntax pick
  (lambda (stx)
    (syntax-case stx (else)
      [(_ else) (syntax 'literal-else)]
      [(_) (syntax 'empty)]
      [(_ x) (syntax x)]
      [(_ x y ...) (syntax (list x (list y ...)))])))

(report 'pick-empty (pick))
(report 'pick-literal (pick else))
(report 'pick-one (pick 1))
(report 'pick-many (pick 1 2 3))

(define-syntax deconstruct
  (lambda (stx)
    (syntax-case stx ()
      [(_) (syntax 'empty)]
      [(_ a b rest ...) (syntax (list 'pair a b 'tail (list rest ...)))])))

(report 'deconstruct-empty (deconstruct))
(report 'deconstruct-pair (deconstruct 1 2 3 4))

(define-syntax nested-pairs
  (lambda (stx)
    (syntax-case stx ()
      [(_ ((k v) ...)) (syntax (list (cons 'k v) ...))])))

(report 'nested (nested-pairs ((a 1) (b 2))))

(define-syntax vector-pattern
  (lambda (stx)
    (syntax-case stx ()
      [(_ #(a b)) (syntax (list a b))])))

(report 'vector (vector-pattern #(1 2)))

; --- with-syntax ---
(define-syntax with-sum
  (lambda (stx)
    (syntax-case stx ()
      [(_ x y)
       (with-syntax ((total (syntax (+ x y))))
         (syntax (list total total)))])))

(report 'with-syntax (with-sum 3 4))

(define-syntax with-items
  (lambda (stx)
    (syntax-case stx ()
      [(_ item ...)
       (with-syntax (((x ...) (syntax (item ...))))
         (syntax (list x ...)))])))

(report 'with-syntax-ellipsis (with-items 1 2 3))

; --- recursive expansion ---
(define-syntax my-or
  (lambda (stx)
    (syntax-case stx ()
      [(_) (syntax #f)]
      [(_ e) (syntax e)]
      [(_ e rest ...)
       (syntax (let ((t e)) (if t t (my-or rest ...))))])))

(report 'or-empty (my-or))
(report 'or-value (my-or 7))
(report 'or-tail (my-or #f #f 9))

; --- hygiene ---
(define hygienic-tmp 'outer)
(define-syntax hygienic-let
  (lambda (stx)
    (syntax-case stx ()
      [(_ body)
       (with-syntax (((tmp) (generate-temporaries (syntax (tmp)))))
         (syntax (let ((tmp 'inner)) body)))])))

(report 'hygiene-user (hygienic-let hygienic-tmp))

(define-syntax introduce-binding
  (lambda (stx)
    (syntax-case stx ()
      [(_) (syntax (let ((secret 'inner)) secret))])))

(report 'hygiene-introduced (introduce-binding))

(define (site-helper x) (* x 100))
(define-syntax site-call
  (lambda (stx)
    (syntax-case stx ()
      [(_ x) (syntax (site-helper x))])))

(report 'definition-site
        (let ((site-helper (lambda (x) 'shadowed)))
          (site-call 2)))

(define nested-secret 'outer)
(define-syntax make-getter
  (lambda (stx)
    (syntax-case stx ()
      [(_ getter body)
       (syntax
         (let ((secret 'inner))
           (let-syntax ((getter
                          (lambda (s)
                            (syntax-case s ()
                              [(_) (syntax secret)]))))
             body)))])))

(report 'nested-macro (make-getter g (g)))

; --- syntax helpers ---
(define-syntax identifier-kind
  (lambda (stx)
    (syntax-case stx ()
      [(_ x)
       (if (identifier? (syntax x))
           (syntax 'identifier)
           (syntax 'datum))])))

(report 'identifier-true (identifier-kind foo))
(report 'identifier-false (identifier-kind 42))

(define-syntax same-datum?
  (lambda (stx)
    (syntax-case stx ()
      [(_ x y)
       (if (equal? (syntax->datum (syntax x))
                   (syntax->datum (syntax y)))
           (syntax #t)
           (syntax #f))])))

(report 'datum-equal (same-datum? (a b) (a b)))
(report 'datum-diff (same-datum? (a b) (a c)))

(define-syntax count-items
  (lambda (stx)
    (syntax-case stx ()
      [(_ item ...) (length (syntax->datum (syntax (item ...))))])))

(report 'syntax-list-count (count-items a b c))

(define-syntax quote-name
  (lambda (stx)
    (syntax-case stx ()
      [(_ name)
       (datum->syntax (syntax name)
         (list 'quote (syntax->datum (syntax name))))])))

(report 'datum->syntax (quote-name foo))

(define-syntax free-same?
  (lambda (stx)
    (syntax-case stx ()
      [(_ a b)
       (if (free-identifier=? (syntax a) (syntax b))
           (syntax #t)
           (syntax #f))])))

(report 'free-same (free-same? foo foo))
(report 'free-diff (free-same? foo bar))

(define-syntax bound-same?
  (lambda (stx)
    (syntax-case stx ()
      [(_ a b)
       (if (bound-identifier=? (syntax a) (syntax b))
           (syntax #t)
           (syntax #f))])))

(report 'bound-same (bound-same? foo foo))
(report 'bound-diff (bound-same? foo bar))

(define-syntax temporaries-distinct
  (lambda (stx)
    (syntax-case stx ()
      [(_)
       (let ((ids (generate-temporaries (syntax (a b)))))
         (if (equal? (syntax->datum (car ids))
                     (syntax->datum (car (cdr ids))))
             (syntax #f)
             (syntax #t)))])))

(report 'temporaries-distinct (temporaries-distinct))

; --- expansion-time computation ---
(define-syntax sum-to
  (lambda (stx)
    (syntax-case stx ()
      [(_ n)
       (let ((count (syntax->datum (syntax n))))
         (let loop ((i 0) (total 0))
           (if (= i count) total (loop (+ i 1) (+ total i)))))])))

(report 'computed (sum-to 5))

; --- local macros ---
(define (local-macro)
  (let-syntax ((double
                 (lambda (stx)
                   (syntax-case stx ()
                     [(_ x) (syntax (* 2 x))]))))
    (double 21)))

(report 'local-macro (local-macro))

; --- top-level definitions produced by transformers ---
(define-syntax define-constant
  (lambda (stx)
    (syntax-case stx ()
      [(_ name value) (syntax (define name value))])))

(define-constant procedural-answer 42)
(report 'top-level-define procedural-answer)

(define-syntax define-two
  (lambda (stx)
    (syntax-case stx ()
      [(_ a b) (syntax (begin (define a 1) (define b 2)))])))

(define-two first-value second-value)
(report 'top-level-splice (list first-value second-value))

(define-syntax define-identity
  (lambda (stx)
    (syntax-case stx ()
      [(_ name)
       (syntax (define-syntax name (syntax-rules () ((_ x) x))))])))

(define-identity identity-macro)
(report 'generated-macro (identity-macro 7))

; --- quasisyntax ---
(define-syntax quasi-list
  (lambda (stx)
    (syntax-case stx ()
      [(_ x ...)
       (quasisyntax (list (unsyntax-splicing (syntax (x ...)))))])))

(report 'quasisyntax (quasi-list 1 2 3))

(define-syntax quasi-one
  (lambda (stx)
    (syntax-case stx ()
      [(_ x) (quasisyntax (list (unsyntax (syntax x)) 1))])))

(report 'quasisyntax-unsyntax (quasi-one 9))

; --- definitions in bodies and mixed begins ---
(define-syntax define-local-pair
  (lambda (stx)
    (syntax-case stx ()
      [(_ a b)
       (syntax (begin (define a 1) (define b (+ a 1))))])))

(define (body-definitions)
  (define-local-pair local-a local-b)
  (list local-a local-b))

(report 'body-definitions (body-definitions))

(define-syntax define-local-macro
  (lambda (stx)
    (syntax-case stx ()
      [(_ name)
       (syntax (define-syntax name
                 (syntax-rules () ((_ x) (+ x 1)))))])))

(define (local-macro-definition)
  (define-local-macro plus-one)
  (plus-one 41))

(report 'local-macro-definition (local-macro-definition))

(define-syntax define-and-return
  (lambda (stx)
    (syntax-case stx ()
      [(_ name value) (syntax (begin (define name value) name))])))

(define (mixed-begin)
  (define-and-return mixed 42)
  mixed)

(report 'mixed-begin (mixed-begin))

(define-syntax bind-two
  (lambda (stx)
    (syntax-case stx ()
      [(_)
       (syntax (let-values (((a b) (values 20 22))) (+ a b)))])))

(report 'let-values (bind-two))

; --- shadowing and nested expansion ---
(define-syntax shadowed
  (lambda (stx)
    (syntax-case stx ()
      [(_ x) (syntax (+ x 1))])))

(define (macro-shadow)
  (let ((shadowed (lambda (x) (* x 2))))
    (shadowed 21)))

(report 'macro-shadow (macro-shadow))

(define (body-macro-shadow)
  (let-syntax ((pick
                 (lambda (stx)
                   (syntax-case stx ()
                     [(_ x) (syntax (* x 3))]))))
    (pick 14)))

(report 'body-macro-shadow (body-macro-shadow))

(define-syntax inner-macro
  (lambda (stx)
    (syntax-case stx ()
      [(_ x) (syntax (+ x 2))])))

(define-syntax outer-macro
  (lambda (stx)
    (syntax-case stx ()
      [(_ x) (syntax (inner-macro x))])))

(report 'nested-expansion (outer-macro 40))

(begin
  (define-syntax even-steps?
    (lambda (stx)
      (syntax-case stx ()
        [(_) (syntax #t)]
        [(_ x rest ...) (syntax (odd-steps? rest ...))])))

  (define-syntax odd-steps?
    (lambda (stx)
      (syntax-case stx ()
        [(_) (syntax #f)]
        [(_ x rest ...) (syntax (even-steps? rest ...))]))))

(report 'mutual-macros
        (list (even-steps? a b) (odd-steps? a b c)))

(define-syntax flattened-pairs
  (lambda (stx)
    (syntax-case stx ()
      [(_ ((a b) ...)) (syntax (list (list a b) ...))])))

(report 'nested-template (flattened-pairs ((1 2) (3 4))))

(define-syntax depth-two
  (lambda (stx)
    (syntax-case stx ()
      [(_ ((a ...) ...)) (syntax (list (list a ...) ...))])))

(report 'depth-two (depth-two ((1 2) (3 4 5))))

(define-syntax depth-zero
  (lambda (stx)
    (syntax-case stx ()
      [(_ n x ...) (syntax (list (list n x) ...))])))

(report 'depth-zero (depth-zero 7 1 2))

(define-syntax two-groups
  (lambda (stx)
    (syntax-case stx ()
      [(_ (a ...) (b ...))
       (syntax (list (list a ...) (list b ...)))])))

(report 'two-groups (two-groups (1 2) (3 4 5)))

(define-syntax chained-with
  (lambda (stx)
    (syntax-case stx ()
      [(_ x)
       (with-syntax ((y (syntax (+ x 1))))
         (with-syntax ((z (syntax (* y 2))))
           (syntax z)))])))

(report 'chained-with (chained-with 20))

(define-syntax literal-check
  (lambda (stx)
    (syntax-case stx (tag)
      [(_ tag) (syntax 'tag-literal)]
      [(_ x) (syntax 'other)])))

(define (literal-shadow)
  (let ((tag 1))
    (literal-check tag)))

(report 'literal-shadow (literal-shadow))
(report 'literal-unbound (literal-check tag))

(define-syntax make-adder
  (lambda (stx)
    (syntax-case stx ()
      [(_ n) (syntax (lambda (x) (+ x n)))])))

(report 'introduced-formal ((make-adder 40) 2))

(define-syntax empty-items
  (lambda (stx)
    (syntax-case stx ()
      [(_ item ...)
       (with-syntax (((x ...) (syntax (item ...))))
         (syntax (list x ...)))])))

(report 'empty-ellipsis (empty-items))

(define-syntax datum-vector-equal?
  (lambda (stx)
    (syntax-case stx ()
      [(_)
       (if (equal? (syntax->datum (syntax #(a "b" 3)))
                   (syntax->datum (syntax #(a "b" 3))))
           (syntax #t)
           (syntax #f))])))

(report 'syntax-datum-vector (datum-vector-equal?))

(define counter-log '())
(define-syntax define-hidden-counter
  (lambda (stx)
    (syntax-case stx ()
      [(_)
       (syntax (begin
                 (define hidden 1)
                 (set! hidden (+ hidden 1))
                 (set! counter-log (cons hidden counter-log))))])))

(define-hidden-counter)
(define-hidden-counter)
(report 'top-level-counter (reverse counter-log))
