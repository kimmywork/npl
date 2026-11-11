; match (.agents/archive/design/design-data.md section 4): the pattern language NPL shares
; with Guile's (ice-9 match), both Alex Shinn's matcher. Literals compare
; as equal? does; ... binds lists and needs a proper list; records match
; by field position with $; clauses are tried in order.
(define (report name value) (write (list name value)) (newline))

(define-record-type point (make-point x y) point?
  (x point-x)
  (y point-y))

(report 'variable (match 5 (x x)))
(report 'wildcard (match '(1 2) ((_ b) b)))
(report 'list (match '(1 2 3) ((a b c) (list c b a))))
(report 'improper (match '(1 2 . 3) ((a . rest) (list a rest))))
(report 'null (list (match '() (() 'empty) (_ 'other))
                    (match '(1) (() 'empty) (_ 'other))))
(report 'clause-order (match '(1 2) ((a . b) 'pair) ((a b) 'list)))
(report 'literals
  (map (lambda (x)
         (match x
           (1 'one) (2.5 'flonum) ("s" 'string) (#\c 'char)
           (#t 'true) (#f 'false) (_ 'other)))
       (list 1 2.5 "s" #\c #t #f 'z "t" 1.0)))
(report 'big-literal
  (match 100000000000000000000 (100000000000000000000 'bignum) (_ 'no)))
(report 'bytevector-literal
  (map (lambda (x) (match x (#u8(1 2) 'bv) (_ 'other)))
       (list (bytevector 1 2) (bytevector 1 3) (bytevector 1) "s")))
(report 'quote
  (map (lambda (x)
         (match x ('a 'sym) ('(b "c") 'list) ('#(1 d) 'vector) (_ 'other)))
       (list 'a (list 'b "c") (vector 1 'd) '(b d) (vector 1 'e))))
(report 'ellipsis (match '(1 2 3) ((x ...) x)))
(report 'ellipsis-tail (match '(1 2 3 4 5) ((a b ... c d) (list a b c d))))
(report 'ellipsis-nested (match '((a 1) (b 2)) (((k v) ...) (list k v))))
(report 'ellipsis-empty (match '(1) ((a b ...) (list a b))))
(report 'ellipsis-improper (match '(1 2 . 3) ((x ...) x) (_ 'not-a-list)))
(report 'ellipsis-circular
  (let ((c (list 1 2)))
    (set-cdr! (cdr c) c)
    (match c ((x ...) 'list) (_ 'circular))))
(report 'ellipsis-element-mismatch
  (match '(1 2 x) (((? number? n) ...) n) (_ 'no)))
(report 'ellipsis-too-short (match '(1) ((a b ... c d) 'long) (_ 'short)))
(report 'one-or-more
  (list (match '(1 2) ((x ..1) x)) (match '() ((x ..1) x) (_ 'none))))
(report 'underscores (match '(1 2 3) ((a ___) a)))
(report 'vector (match (vector 1 2 3) (#(a b c) (+ a b c))))
(report 'vector-ellipsis (match (vector 1 2 3 4) (#(a b ...) (list a b))))
(report 'vector-length
  (match (vector 1 2) (#(a b c) 'three) (#(a b) 'two)))
(report 'and (match '(1 2) ((and whole (a b)) (list whole a b))))
(report 'or
  (map (lambda (x)
         (match x ((or 1 2) 'small) ((or (a) (a _)) a) (_ 'other)))
       (list 1 2 '(3) '(4 5) 6)))
(report 'not (map (lambda (x) (match x ((not 1) 'not-one) (_ 'one)))
                  (list 1 2)))
(report 'predicate
  (match '(1 "a") (((? number? n) (? string? s)) (list n s))))
(report 'predicate-patterns (match 10 ((? number? (? positive?) n) n)))
(report 'apply (match '(1 2 3) ((= length n) n)))
(report 'apply-nested
  (match '((1 2) 3) (((= car first) . rest) (list first rest))))
(report 'quasiquote
  (match '(add 1 (mul 2 3)) (`(add ,x (mul ,y ,z)) (list x y z))))
(report 'quasiquote-vector (match (vector 'v 1) (`#(v ,x) x)))
(report 'tree-search (match '(a (b (c d))) ((x *** 'd) x)))
(report 'record (match (make-point 1 2) (($ point a b) (list a b))))
(report 'record-prefix (match (make-point 1 2) (($ point a) a)))
(report 'record-other
  (map (lambda (x) (match x (($ point a b) 'point) (_ 'other)))
       (list (vector 1 2) '(1 2))))
(report 'record-nested
  (match (list (make-point 1 (make-point 2 3)))
    ((($ point a ($ point b c))) (list a b c))))
(report 'failure-continuation
  (map (lambda (x)
         (match x
           ((a b) (=> next) (if (= a 1) (next) 'first))
           (_ 'second)))
       (list '(1 2) '(3 4))))
(report 'set-pattern
  (let ((x (list 1 2)))
    (match x ((a (set! set-second)) (set-second 20)))
    x))
(report 'get-pattern
  (match (list 1 (vector 2)) ((a #((get! get))) (get))))
(define area
  (match-lambda
    (('square s) (* s s))
    (('rect w h) (* w h))))
(report 'match-lambda (list (area '(square 3)) (area '(rect 2 5))))
(report 'match-lambda* ((match-lambda* ((a b) (+ a b)) ((a) a)) 1 2))
(report 'match-let (match-let (((a b) '(1 2)) (#(c) (vector 3)) (d 4))
                     (list a b c d)))
(report 'match-let* (match-let* (((a b) '(1 2)) ((c . d) (list b a)))
                      (list a b c d)))
(report 'match-letrec
  (match-letrec (((even? odd?)
                  (list (lambda (n) (if (= n 0) #t (odd? (- n 1))))
                        (lambda (n) (if (= n 0) #f (even? (- n 1)))))))
    (list (even? 10) (odd? 7))))
(report 'named-match-let
  (match-let loop (((a . b) '(1 2 3)) (total 0))
    (if (null? b) (+ total a) (loop b (+ total a)))))
(report 'hygiene
  (let ((v 'v) (w 'w) (loop 'loop) (failure 'failure) (pair? (lambda (x) #f)))
    (match '(1 2) ((a b) (list v w loop failure a b)))))
(report 'no-match (guard (e (#t 'caught)) (match 3 (1 'one))))
