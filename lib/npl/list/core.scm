;;; SRFI-1 list-processing library			-*- Scheme -*-
;;; Reference implementation
;;;
;;; SPDX-License-Identifier: MIT
;;;
;;; Copyright (c) 1998, 1999 by Olin Shivers. You may do as you please with
;;; this code as long as you do not remove this copyright notice or
;;; hold me liable for its use. Please send bug reports to shivers@ai.mit.edu.

; Portable helpers required by the SRFI-1 reference implementation.
(define (check-arg pred val caller)
  (if (pred val) val (error "Bad argument" val pred caller)))

(define-syntax receive
  (syntax-rules ()
    ((_ formals expr body ...)
     (call-with-values (lambda () expr) (lambda formals body ...)))))

(define-syntax let-optionals
  (syntax-rules ()
    ((_ args () body ...) (begin body ...))
    ((_ args ((var default) rest ...) body ...)
     (let ((var (if (pair? args) (car args) default)))
       (let-optionals (if (pair? args) (cdr args) '()) (rest ...) body ...)))))

(define (:optional maybe-default default)
  (if (pair? maybe-default) (car maybe-default) default))

;;; Constructors
;;;;;;;;;;;;;;;;

;;; Occasionally useful as a value to be passed to a fold or other
;;; higher-order procedure.
(define (xcons d a) (cons a d))

;;;; Recursively copy every cons.
;(define (tree-copy x)
;  (let recur ((x x))
;    (if (not (pair? x)) x
;	(cons (recur (car x)) (recur (cdr x))))))

;;; Make a list of length LEN.

(define (make-list len . maybe-elt)
  (check-arg (lambda (n) (and (integer? n) (>= n 0))) len make-list)
  (let ((elt (cond ((null? maybe-elt) #f) ; Default value
		   ((null? (cdr maybe-elt)) (car maybe-elt))
		   (else (error "Too many arguments to MAKE-LIST"
				(cons len maybe-elt))))))
    (do ((i len (- i 1))
	 (ans '() (cons elt ans)))
	((<= i 0) ans))))


;(define (list . ans) ans)	; R4RS


;;; Make a list of length LEN. Elt i is (PROC i) for 0 <= i < LEN.

(define (list-tabulate len proc)
  (check-arg (lambda (n) (and (integer? n) (>= n 0))) len list-tabulate)
  (check-arg procedure? proc list-tabulate)
  (do ((i (- len 1) (- i 1))
       (ans '() (cons (proc i) ans)))
      ((< i 0) ans)))

;;; (cons* a1 a2 ... an) = (cons a1 (cons a2 (cons ... an)))
;;; (cons* a1) = a1	(cons* a1 a2 ...) = (cons a1 (cons* a2 ...))
;;;
;;; (cons first (unfold not-pair? car cdr rest values))

(define (cons* first . rest)
  (let recur ((x first) (rest rest))
    (if (pair? rest)
	(cons x (recur (car rest) (cdr rest)))
	x)))

;;; (unfold not-pair? car cdr lis values)

(define (list-copy lis)
  (let recur ((lis lis))
    (if (pair? lis)
	(cons (car lis) (recur (cdr lis)))
	lis)))

;;; IOTA count [start step]	(start start+step ... start+(count-1)*step)

(define (iota count . maybe-start+step)
  (check-arg integer? count iota)
  (if (< count 0) (error "Negative step count" iota count))
  (let-optionals maybe-start+step ((start 0) (step 1))
    (check-arg number? start iota)
    (check-arg number? step iota)
    (let loop ((n 0) (r '()))
      (if (= n count)
	  (reverse r)
	  (loop (+ 1 n)
		(cons (+ start (* n step)) r))))))

;;; I thought these were lovely, but the public at large did not share my
;;; enthusiasm...
;;; :IOTA to		(0 ... to-1)
;;; :IOTA from to	(from ... to-1)
;;; :IOTA from to step  (from from+step ...)

;;; IOTA: to		(1 ... to)
;;; IOTA: from to	(from+1 ... to)
;;; IOTA: from to step	(from+step from+2step ...)

;(define (%parse-iota-args arg1 rest-args proc)
;  (let ((check (lambda (n) (check-arg integer? n proc))))
;    (check arg1)
;    (if (pair? rest-args)
;	(let ((arg2 (check (car rest-args)))
;	      (rest (cdr rest-args)))
;	  (if (pair? rest)
;	      (let ((arg3 (check (car rest)))
;		    (rest (cdr rest)))
;		(if (pair? rest) (error "Too many parameters" proc arg1 rest-args)
;		    (values arg1 arg2 arg3)))
;	      (values arg1 arg2 1)))
;	(values 0 arg1 1))))
;
;(define (iota: arg1 . rest-args)
;  (receive (from to step) (%parse-iota-args arg1 rest-args iota:)
;    (let* ((numsteps (floor (/ (- to from) step)))
;	   (last-val (+ from (* step numsteps))))
;      (if (< numsteps 0) (error "Negative step count" iota: from to step))
;      (do ((steps-left numsteps (- steps-left 1))
;	   (val last-val (- val step))
;	   (ans '() (cons val ans)))
;	  ((<= steps-left 0) ans)))))
;
;
;(define (:iota arg1 . rest-args)
;  (receive (from to step) (%parse-iota-args arg1 rest-args :iota)
;    (let* ((numsteps (ceiling (/ (- to from) step)))
;	   (last-val (+ from (* step (- numsteps 1)))))
;      (if (< numsteps 0) (error "Negative step count" :iota from to step))
;      (do ((steps-left numsteps (- steps-left 1))
;	   (val last-val (- val step))
;	   (ans '() (cons val ans)))
;	  ((<= steps-left 0) ans)))))



(define (circular-list val1 . vals)
  (let ((ans (cons val1 vals)))
    (set-cdr! (last-pair ans) ans)
    ans))

;;; <proper-list> ::= ()			; Empty proper list
;;;		  |   (cons <x> <proper-list>)	; Proper-list pair
;;; Note that this definition rules out circular lists -- and this
;;; function is required to detect this case and return false.

(define (proper-list? x)
  (let lp ((x x) (lag x))
    (if (pair? x)
	(let ((x (cdr x)))
	  (if (pair? x)
	      (let ((x   (cdr x))
		    (lag (cdr lag)))
		(and (not (eq? x lag)) (lp x lag)))
	      (null? x)))
	(null? x))))


;;; A dotted list is a finite list (possibly of length 0) terminated
;;; by a non-nil value. Any non-cons, non-nil value (e.g., "foo" or 5)
;;; is a dotted list of length 0.
;;;
;;; <dotted-list> ::= <non-nil,non-pair>	; Empty dotted list
;;;               |   (cons <x> <dotted-list>)	; Proper-list pair

(define (dotted-list? x)
  (let lp ((x x) (lag x))
    (if (pair? x)
	(let ((x (cdr x)))
	  (if (pair? x)
	      (let ((x   (cdr x))
		    (lag (cdr lag)))
		(and (not (eq? x lag)) (lp x lag)))
	      (not (null? x))))
	(not (null? x)))))

(define (circular-list? x)
  (let lp ((x x) (lag x))
    (and (pair? x)
	 (let ((x (cdr x)))
	   (and (pair? x)
		(let ((x   (cdr x))
		      (lag (cdr lag)))
		  (or (eq? x lag) (lp x lag))))))))

(define (not-pair? x) (not (pair? x)))	; Inline me.

;;; This is a legal definition which is fast and sloppy:
;;;     (define null-list? not-pair?)
;;; but we'll provide a more careful one:
(define (null-list? l)
  (cond ((pair? l) #f)
	((null? l) #t)
	(else (error "null-list?: argument out of domain" l))))


(define (list= = . lists)
  (or (null? lists) ; special case

      (let lp1 ((list-a (car lists)) (others (cdr lists)))
	(or (null? others)
	    (let ((list-b (car others))
		  (others (cdr others)))
	      (if (eq? list-a list-b)	; EQ? => LIST=
		  (lp1 list-b others)
		  (let lp2 ((pair-a list-a) (pair-b list-b))
		    (if (null-list? pair-a)
			(and (null-list? pair-b)
			     (lp1 list-b others))
			(and (not (null-list? pair-b))
			     (= (car pair-a) (car pair-b))
			     (lp2 (cdr pair-a) (cdr pair-b)))))))))))


;;; R4RS, so commented out.
;(define (length x)			; LENGTH may diverge or
;  (let lp ((x x) (len 0))		; raise an error if X is
;    (if (pair? x)			; a circular list. This version
;        (lp (cdr x) (+ len 1))		; diverges.
;        len)))

(define (length+ x)			; Returns #f if X is circular.
  (let lp ((x x) (lag x) (len 0))
    (if (pair? x)
	(let ((x (cdr x))
	      (len (+ len 1)))
	  (if (pair? x)
	      (let ((x   (cdr x))
		    (lag (cdr lag))
		    (len (+ len 1)))
		(and (not (eq? x lag)) (lp x lag len)))
	      len))
	len)))

(define (zip list1 . more-lists) (apply map list list1 more-lists))


;;; Selectors
;;;;;;;;;;;;;

;;; R4RS non-primitives:
;(define (caar   x) (car (car x)))
;(define (cadr   x) (car (cdr x)))
;(define (cdar   x) (cdr (car x)))
;(define (cddr   x) (cdr (cdr x)))
;
;(define (caaar  x) (caar (car x)))
;(define (caadr  x) (caar (cdr x)))
;(define (cadar  x) (cadr (car x)))
;(define (caddr  x) (cadr (cdr x)))
;(define (cdaar  x) (cdar (car x)))
;(define (cdadr  x) (cdar (cdr x)))
;(define (cddar  x) (cddr (car x)))
;(define (cdddr  x) (cddr (cdr x)))
;
;(define (caaaar x) (caaar (car x)))
;(define (caaadr x) (caaar (cdr x)))
;(define (caadar x) (caadr (car x)))
;(define (caaddr x) (caadr (cdr x)))
;(define (cadaar x) (cadar (car x)))
;(define (cadadr x) (cadar (cdr x)))
;(define (caddar x) (caddr (car x)))
;(define (cadddr x) (caddr (cdr x)))
;(define (cdaaar x) (cdaar (car x)))
;(define (cdaadr x) (cdaar (cdr x)))
;(define (cdadar x) (cdadr (car x)))
;(define (cdaddr x) (cdadr (cdr x)))
;(define (cddaar x) (cddar (car x)))
;(define (cddadr x) (cddar (cdr x)))
;(define (cdddar x) (cdddr (car x)))
;(define (cddddr x) (cdddr (cdr x)))


(define first  car)
(define second cadr)
(define third  caddr)
(define fourth cadddr)
(define (fifth   x) (car    (cddddr x)))
(define (sixth   x) (cadr   (cddddr x)))
(define (seventh x) (caddr  (cddddr x)))
(define (eighth  x) (cadddr (cddddr x)))
(define (ninth   x) (car  (cddddr (cddddr x))))
(define (tenth   x) (cadr (cddddr (cddddr x))))

(define (car+cdr pair) (values (car pair) (cdr pair)))

;;; take & drop

(define (take lis k)
  (check-arg integer? k take)
  (let recur ((lis lis) (k k))
    (if (zero? k) '()
	(cons (car lis)
	      (recur (cdr lis) (- k 1))))))

(define (drop lis k)
  (check-arg integer? k drop)
  (let iter ((lis lis) (k k))
    (if (zero? k) lis (iter (cdr lis) (- k 1)))))

(define (take! lis k)
  (check-arg integer? k take!)
  (if (zero? k) '()
      (begin (set-cdr! (drop lis (- k 1)) '())
	     lis)))

;;; TAKE-RIGHT and DROP-RIGHT work by getting two pointers into the list,
;;; off by K, then chasing down the list until the lead pointer falls off
;;; the end.

(define (take-right lis k)
  (check-arg integer? k take-right)
  (let lp ((lag lis)  (lead (drop lis k)))
    (if (pair? lead)
	(lp (cdr lag) (cdr lead))
	lag)))

(define (drop-right lis k)
  (check-arg integer? k drop-right)
  (let recur ((lag lis) (lead (drop lis k)))
    (if (pair? lead)
	(cons (car lag) (recur (cdr lag) (cdr lead)))
	'())))

;;; In this function, LEAD is actually K+1 ahead of LAG. This lets
;;; us stop LAG one step early, in time to smash its cdr to ().
(define (drop-right! lis k)
  (check-arg integer? k drop-right!)
  (let ((lead (drop lis k)))
    (if (pair? lead)

	(let lp ((lag lis)  (lead (cdr lead)))	; Standard case
	  (if (pair? lead)
	      (lp (cdr lag) (cdr lead))
	      (begin (set-cdr! lag '())
		     lis)))

	'())))	; Special case dropping everything -- no cons to side-effect.

;(define (list-ref lis i) (car (drop lis i)))	; R4RS

;;; These use the APL convention, whereby negative indices mean
;;; "from the right." I liked them, but they didn't win over the
;;; SRFI reviewers.
;;; K >= 0: Take and drop  K elts from the front of the list.
;;; K <= 0: Take and drop -K elts from the end   of the list.

;(define (take lis k)
;  (check-arg integer? k take)
;  (if (negative? k)
;      (list-tail lis (+ k (length lis)))
;      (let recur ((lis lis) (k k))
;	(if (zero? k) '()
;	    (cons (car lis)
;		  (recur (cdr lis) (- k 1)))))))
;
;(define (drop lis k)
;  (check-arg integer? k drop)
;  (if (negative? k)
;      (let recur ((lis lis) (nelts (+ k (length lis))))
;	(if (zero? nelts) '()
;	    (cons (car lis)
;		  (recur (cdr lis) (- nelts 1)))))
;      (list-tail lis k)))
;
;
;(define (take! lis k)
;  (check-arg integer? k take!)
;  (cond ((zero? k) '())
;	((positive? k)
;	 (set-cdr! (list-tail lis (- k 1)) '())
;	 lis)
;	(else (list-tail lis (+ k (length lis))))))
;
;(define (drop! lis k)
;  (check-arg integer? k drop!)
;  (if (negative? k)
;      (let ((nelts (+ k (length lis))))
;	(if (zero? nelts) '()
;	    (begin (set-cdr! (list-tail lis (- nelts 1)) '())
;		   lis)))
;      (list-tail lis k)))

(define (split-at x k)
  (check-arg integer? k split-at)
  (let recur ((lis x) (k k))
    (if (zero? k) (values '() lis)
	(receive (prefix suffix) (recur (cdr lis) (- k 1))
	  (values (cons (car lis) prefix) suffix)))))

(define (split-at! x k)
  (check-arg integer? k split-at!)
  (if (zero? k) (values '() x)
      (let* ((prev (drop x (- k 1)))
	     (suffix (cdr prev)))
	(set-cdr! prev '())
	(values x suffix))))


(define (last lis) (car (last-pair lis)))

(define (last-pair lis)
  (check-arg pair? lis last-pair)
  (let lp ((lis lis))
    (let ((tail (cdr lis)))
      (if (pair? tail) (lp tail) lis))))


;;; Unzippers -- 1 through 5
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (unzip1 lis) (map car lis))

(define (unzip2 lis)
  (let recur ((lis lis))
    (if (null-list? lis) (values lis lis)	; Use NOT-PAIR? to handle
	(let ((elt (car lis)))			; dotted lists.
	  (receive (a b) (recur (cdr lis))
	    (values (cons (car  elt) a)
		    (cons (cadr elt) b)))))))

(define (unzip3 lis)
  (let recur ((lis lis))
    (if (null-list? lis) (values lis lis lis)
	(let ((elt (car lis)))
	  (receive (a b c) (recur (cdr lis))
	    (values (cons (car   elt) a)
		    (cons (cadr  elt) b)
		    (cons (caddr elt) c)))))))

(define (unzip4 lis)
  (let recur ((lis lis))
    (if (null-list? lis) (values lis lis lis lis)
	(let ((elt (car lis)))
	  (receive (a b c d) (recur (cdr lis))
	    (values (cons (car    elt) a)
		    (cons (cadr   elt) b)
		    (cons (caddr  elt) c)
		    (cons (cadddr elt) d)))))))

(define (unzip5 lis)
  (let recur ((lis lis))
    (if (null-list? lis) (values lis lis lis lis lis)
	(let ((elt (car lis)))
	  (receive (a b c d e) (recur (cdr lis))
	    (values (cons (car     elt) a)
		    (cons (cadr    elt) b)
		    (cons (caddr   elt) c)
		    (cons (cadddr  elt) d)
		    (cons (car (cddddr  elt)) e)))))))


;;; append! append-reverse append-reverse! concatenate concatenate!
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (append! . lists)
  ;; First, scan through lists looking for a non-empty one.
  (let lp ((lists lists) (prev '()))
    (if (not (pair? lists)) prev
	(let ((first (car lists))
	      (rest (cdr lists)))
	  (if (not (pair? first)) (lp rest first)

	      ;; Now, do the splicing.
	      (let lp2 ((tail-cons (last-pair first))
			(rest rest))
		(if (pair? rest)
		    (let ((next (car rest))
			  (rest (cdr rest)))
		      (set-cdr! tail-cons next)
		      (lp2 (if (pair? next) (last-pair next) tail-cons)
			   rest))
		    first)))))))

;;; APPEND is R4RS.
;(define (append . lists)
;  (if (pair? lists)
;      (let recur ((list1 (car lists)) (lists (cdr lists)))
;        (if (pair? lists)
;            (let ((tail (recur (car lists) (cdr lists))))
;              (fold-right cons tail list1)) ; Append LIST1 & TAIL.
;            list1))
;      '()))

;(define (append-reverse rev-head tail) (fold cons tail rev-head))

;(define (append-reverse! rev-head tail)
;  (pair-fold (lambda (pair tail) (set-cdr! pair tail) pair)
;             tail
;             rev-head))

;;; Hand-inline the FOLD and PAIR-FOLD ops for speed.

(define (append-reverse rev-head tail)
  (let lp ((rev-head rev-head) (tail tail))
    (if (null-list? rev-head) tail
	(lp (cdr rev-head) (cons (car rev-head) tail)))))

(define (append-reverse! rev-head tail)
  (let lp ((rev-head rev-head) (tail tail))
    (if (null-list? rev-head) tail
	(let ((next-rev (cdr rev-head)))
	  (set-cdr! rev-head tail)
	  (lp next-rev rev-head)))))


(define (concatenate  lists) (reduce-right append  '() lists))
(define (concatenate! lists) (reduce-right append! '() lists))
