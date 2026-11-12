; SRFI 1 reference implementation, split from the MIT-licensed source.
; Copyright (c) 1998, 1999 by Olin Shivers; SPDX-License-Identifier: MIT.
; See core.scm for the full notice.
;;; Fold/map internal utilities
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; These little internal utilities are used by the general
;;; fold & mapper funs for the n-ary cases . It'd be nice if they got inlined.
;;; One the other hand, the n-ary cases are painfully inefficient as it is.
;;; An aggressive implementation should simply re-write these functions
;;; for raw efficiency; I have written them for as much clarity, portability,
;;; and simplicity as can be achieved.
;;;
;;; I use the dreaded call/cc to do local aborts. A good compiler could
;;; handle this with extreme efficiency. An implementation that provides
;;; a one-shot, non-persistent continuation grabber could help the compiler
;;; out by using that in place of the call/cc's in these routines.
;;;
;;; These functions have funky definitions that are precisely tuned to
;;; the needs of the fold/map procs -- for example, to minimize the number
;;; of times the argument lists need to be examined.

;;; Return (map cdr lists).
;;; However, if any element of LISTS is empty, just abort and return '().
(define (%cdrs lists)
  (call-with-current-continuation
    (lambda (abort)
      (let recur ((lists lists))
	(if (pair? lists)
	    (let ((lis (car lists)))
	      (if (null-list? lis) (abort '())
		  (cons (cdr lis) (recur (cdr lists)))))
	    '())))))

(define (%cars+ lists last-elt)	; (append! (map car lists) (list last-elt))
  (let recur ((lists lists))
    (if (pair? lists) (cons (caar lists) (recur (cdr lists))) (list last-elt))))

;;; LISTS is a (not very long) non-empty list of lists.
;;; Return two lists: the cars & the cdrs of the lists.
;;; However, if any of the lists is empty, just abort and return [() ()].

(define (%cars+cdrs lists)
  (call-with-current-continuation
    (lambda (abort)
      (let recur ((lists lists))
        (if (pair? lists)
	    (receive (list other-lists) (car+cdr lists)
	      (if (null-list? list) (abort '() '()) ; LIST is empty -- bail out
		  (receive (a d) (car+cdr list)
		    (receive (cars cdrs) (recur other-lists)
		      (values (cons a cars) (cons d cdrs))))))
	    (values '() '()))))))

;;; Like %CARS+CDRS, but we pass in a final elt tacked onto the end of the
;;; cars list. What a hack.
(define (%cars+cdrs+ lists cars-final)
  (call-with-current-continuation
    (lambda (abort)
      (let recur ((lists lists))
        (if (pair? lists)
	    (receive (list other-lists) (car+cdr lists)
	      (if (null-list? list) (abort '() '()) ; LIST is empty -- bail out
		  (receive (a d) (car+cdr list)
		    (receive (cars cdrs) (recur other-lists)
		      (values (cons a cars) (cons d cdrs))))))
	    (values (list cars-final) '()))))))

;;; Like %CARS+CDRS, but blow up if any list is empty.
(define (%cars+cdrs/no-test lists)
  (let recur ((lists lists))
    (if (pair? lists)
	(receive (list other-lists) (car+cdr lists)
	  (receive (a d) (car+cdr list)
	    (receive (cars cdrs) (recur other-lists)
	      (values (cons a cars) (cons d cdrs)))))
	(values '() '()))))


;;; count
;;;;;;;;;
(define (count pred list1 . lists)
  (check-arg procedure? pred count)
  (if (pair? lists)

      ;; N-ary case
      (let lp ((list1 list1) (lists lists) (i 0))
	(if (null-list? list1) i
	    (receive (as ds) (%cars+cdrs lists)
	      (if (null? as) i
		  (lp (cdr list1) ds
		      (if (apply pred (car list1) as) (+ i 1) i))))))

      ;; Fast path
      (let lp ((lis list1) (i 0))
	(if (null-list? lis) i
	    (lp (cdr lis) (if (pred (car lis)) (+ i 1) i))))))


;;; fold/unfold
;;;;;;;;;;;;;;;

(define (unfold-right p f g seed . maybe-tail)
  (check-arg procedure? p unfold-right)
  (check-arg procedure? f unfold-right)
  (check-arg procedure? g unfold-right)
  (let lp ((seed seed) (ans (:optional maybe-tail '())))
    (if (p seed) ans
	(lp (g seed)
	    (cons (f seed) ans)))))


(define (unfold p f g seed . maybe-tail-gen)
  (check-arg procedure? p unfold)
  (check-arg procedure? f unfold)
  (check-arg procedure? g unfold)
  (if (pair? maybe-tail-gen)

      (let ((tail-gen (car maybe-tail-gen)))
	(if (pair? (cdr maybe-tail-gen))
	    (apply error "Too many arguments" unfold p f g seed maybe-tail-gen)

	    (let recur ((seed seed))
	      (if (p seed) (tail-gen seed)
		  (cons (f seed) (recur (g seed)))))))

      (let recur ((seed seed))
	(if (p seed) '()
	    (cons (f seed) (recur (g seed)))))))


(define (fold kons knil lis1 . lists)
  (check-arg procedure? kons fold)
  (if (pair? lists)
      (let lp ((lists (cons lis1 lists)) (ans knil))	; N-ary case
	(receive (cars+ans cdrs) (%cars+cdrs+ lists ans)
	  (if (null? cars+ans) ans ; Done.
	      (lp cdrs (apply kons cars+ans)))))

      (let lp ((lis lis1) (ans knil))			; Fast path
	(if (null-list? lis) ans
	    (lp (cdr lis) (kons (car lis) ans))))))


(define (fold-right kons knil lis1 . lists)
  (check-arg procedure? kons fold-right)
  (if (pair? lists)
      (let recur ((lists (cons lis1 lists)))		; N-ary case
	(let ((cdrs (%cdrs lists)))
	  (if (null? cdrs) knil
	      (apply kons (%cars+ lists (recur cdrs))))))

      (let recur ((lis lis1))				; Fast path
	(if (null-list? lis) knil
	    (let ((head (car lis)))
	      (kons head (recur (cdr lis))))))))


(define (pair-fold-right f zero lis1 . lists)
  (check-arg procedure? f pair-fold-right)
  (if (pair? lists)
      (let recur ((lists (cons lis1 lists)))		; N-ary case
	(let ((cdrs (%cdrs lists)))
	  (if (null? cdrs) zero
	      (apply f (append! lists (list (recur cdrs)))))))

      (let recur ((lis lis1))				; Fast path
	(if (null-list? lis) zero (f lis (recur (cdr lis)))))))

(define (pair-fold f zero lis1 . lists)
  (check-arg procedure? f pair-fold)
  (if (pair? lists)
      (let lp ((lists (cons lis1 lists)) (ans zero))	; N-ary case
	(let ((tails (%cdrs lists)))
	  (if (null? tails) ans
	      (lp tails (apply f (append! lists (list ans)))))))

      (let lp ((lis lis1) (ans zero))
	(if (null-list? lis) ans
	    (let ((tail (cdr lis)))		; Grab the cdr now,
	      (lp tail (f lis ans)))))))	; in case F SET-CDR!s LIS.


;;; REDUCE and REDUCE-RIGHT only use RIDENTITY in the empty-list case.
;;; These cannot meaningfully be n-ary.

(define (reduce f ridentity lis)
  (check-arg procedure? f reduce)
  (if (null-list? lis) ridentity
      (fold f (car lis) (cdr lis))))

(define (reduce-right f ridentity lis)
  (check-arg procedure? f reduce-right)
  (if (null-list? lis) ridentity
      (let recur ((head (car lis)) (lis (cdr lis)))
	(if (pair? lis)
	    (f head (recur (car lis) (cdr lis)))
	    head))))



;;; Mappers: append-map append-map! pair-for-each map! filter-map map-in-order
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (append-map f lis1 . lists)
  (really-append-map append-map  append  f lis1 lists))
(define (append-map! f lis1 . lists)
  (really-append-map append-map! append! f lis1 lists))

(define (really-append-map who appender f lis1 lists)
  (check-arg procedure? f who)
  (if (pair? lists)
      (receive (cars cdrs) (%cars+cdrs (cons lis1 lists))
	(if (null? cars) '()
	    (let recur ((cars cars) (cdrs cdrs))
	      (let ((vals (apply f cars)))
		(receive (cars2 cdrs2) (%cars+cdrs cdrs)
		  (if (null? cars2) vals
		      (appender vals (recur cars2 cdrs2))))))))

      ;; Fast path
      (if (null-list? lis1) '()
	  (let recur ((elt (car lis1)) (rest (cdr lis1)))
	    (let ((vals (f elt)))
	      (if (null-list? rest) vals
		  (appender vals (recur (car rest) (cdr rest)))))))))


(define (pair-for-each proc lis1 . lists)
  (check-arg procedure? proc pair-for-each)
  (if (pair? lists)

      (let lp ((lists (cons lis1 lists)))
	(let ((tails (%cdrs lists)))
	  (if (pair? tails)
	      (begin (apply proc lists)
		     (lp tails)))))

      ;; Fast path.
      (let lp ((lis lis1))
	(if (not (null-list? lis))
	    (let ((tail (cdr lis)))	; Grab the cdr now,
	      (proc lis)		; in case PROC SET-CDR!s LIS.
	      (lp tail))))))

;;; We stop when LIS1 runs out, not when any list runs out.
(define (map! f lis1 . lists)
  (check-arg procedure? f map!)
  (if (pair? lists)
      (let lp ((lis1 lis1) (lists lists))
	(if (not (null-list? lis1))
	    (receive (heads tails) (%cars+cdrs/no-test lists)
	      (set-car! lis1 (apply f (car lis1) heads))
	      (lp (cdr lis1) tails))))

      ;; Fast path.
      (pair-for-each (lambda (pair) (set-car! pair (f (car pair)))) lis1))
  lis1)


;;; Map F across L, and save up all the non-false results.
(define (filter-map f lis1 . lists)
  (check-arg procedure? f filter-map)
  (if (pair? lists)
      (let recur ((lists (cons lis1 lists)))
	(receive (cars cdrs) (%cars+cdrs lists)
	  (if (pair? cars)
	      (cond ((apply f cars) => (lambda (x) (cons x (recur cdrs))))
		    (else (recur cdrs))) ; Tail call in this arm.
	      '())))

      ;; Fast path.
      (let recur ((lis lis1))
	(if (null-list? lis) lis
	    (let ((tail (recur (cdr lis))))
	      (cond ((f (car lis)) => (lambda (x) (cons x tail)))
		    (else tail)))))))


;;; Map F across lists, guaranteeing to go left-to-right.
;;; NOTE: Some implementations of R5RS MAP are compliant with this spec;
;;; in which case this procedure may simply be defined as a synonym for MAP.

(define (map-in-order f lis1 . lists)
  (check-arg procedure? f map-in-order)
  (if (pair? lists)
      (let recur ((lists (cons lis1 lists)))
	(receive (cars cdrs) (%cars+cdrs lists)
	  (if (pair? cars)
	      (let ((x (apply f cars)))		; Do head first,
		(cons x (recur cdrs)))		; then tail.
	      '())))

      ;; Fast path.
      (let recur ((lis lis1))
	(if (null-list? lis) lis
	    (let ((tail (cdr lis))
		  (x (f (car lis))))		; Do head first,
	      (cons x (recur tail)))))))	; then tail.


;;; We extend MAP to handle arguments of unequal length.
