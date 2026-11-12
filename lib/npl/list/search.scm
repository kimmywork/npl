; SRFI 1 reference implementation, split from the MIT-licensed source.
; Copyright (c) 1998, 1999 by Olin Shivers; SPDX-License-Identifier: MIT.
; See core.scm for the full notice.
;;; filter, remove, partition
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; FILTER, REMOVE, PARTITION and their destructive counterparts do not
;;; disorder the elements of their argument.

;; This FILTER shares the longest tail of L that has no deleted elements.
;; If Scheme had multi-continuation calls, they could be made more efficient.

(define (filter pred lis)			; Sleazing with EQ? makes this
  (check-arg procedure? pred filter)		; one faster.
  (let recur ((lis lis))
    (if (null-list? lis) lis			; Use NOT-PAIR? to handle dotted lists.
	(let ((head (car lis))
	      (tail (cdr lis)))
	  (if (pred head)
	      (let ((new-tail (recur tail)))	; Replicate the RECUR call so
		(if (eq? tail new-tail) lis
		    (cons head new-tail)))
	      (recur tail))))))			; this one can be a tail call.


;;; Another version that shares longest tail.
;(define (filter pred lis)
;  (receive (ans no-del?)
;      ;; (recur l) returns L with (pred x) values filtered.
;      ;; It also returns a flag NO-DEL? if the returned value
;      ;; is EQ? to L, i.e. if it didn't have to delete anything.
;      (let recur ((l l))
;	(if (null-list? l) (values l #t)
;	    (let ((x  (car l))
;		  (tl (cdr l)))
;	      (if (pred x)
;		  (receive (ans no-del?) (recur tl)
;		    (if no-del?
;			(values l #t)
;			(values (cons x ans) #f)))
;		  (receive (ans no-del?) (recur tl) ; Delete X.
;		    (values ans #f))))))
;    ans))



;(define (filter! pred lis)			; Things are much simpler
;  (let recur ((lis lis))			; if you are willing to
;    (if (pair? lis)				; push N stack frames & do N
;        (cond ((pred (car lis))		; SET-CDR! writes, where N is
;               (set-cdr! lis (recur (cdr lis))); the length of the answer.
;               lis)
;              (else (recur (cdr lis))))
;        lis)))


;;; This implementation of FILTER!
;;; - doesn't cons, and uses no stack;
;;; - is careful not to do redundant SET-CDR! writes, as writes to memory are
;;;   usually expensive on modern machines, and can be extremely expensive on
;;;   modern Schemes (e.g., ones that have generational GC's).
;;; It just zips down contiguous runs of in and out elts in LIS doing the
;;; minimal number of SET-CDR!s to splice the tail of one run of ins to the
;;; beginning of the next.

(define (filter! pred lis)
  (check-arg procedure? pred filter!)
  (let lp ((ans lis))
    (cond ((null-list? ans)       ans)			; Scan looking for
	  ((not (pred (car ans))) (lp (cdr ans)))	; first cons of result.

	  ;; ANS is the eventual answer.
	  ;; SCAN-IN: (CDR PREV) = LIS and (CAR PREV) satisfies PRED.
	  ;;          Scan over a contiguous segment of the list that
	  ;;          satisfies PRED.
	  ;; SCAN-OUT: (CAR PREV) satisfies PRED. Scan over a contiguous
	  ;;           segment of the list that *doesn't* satisfy PRED.
	  ;;           When the segment ends, patch in a link from PREV
	  ;;           to the start of the next good segment, and jump to
	  ;;           SCAN-IN.
	  (else (letrec ((scan-in (lambda (prev lis)
				    (if (pair? lis)
					(if (pred (car lis))
					    (scan-in lis (cdr lis))
					    (scan-out prev (cdr lis))))))
			 (scan-out (lambda (prev lis)
				     (let lp ((lis lis))
				       (if (pair? lis)
					   (if (pred (car lis))
					       (begin (set-cdr! prev lis)
						      (scan-in lis (cdr lis)))
					       (lp (cdr lis)))
					   (set-cdr! prev lis))))))
		  (scan-in ans (cdr ans))
		  ans)))))



;;; Answers share common tail with LIS where possible;
;;; the technique is slightly subtle.

(define (partition pred lis)
  (check-arg procedure? pred partition)
  (let recur ((lis lis))
    (if (null-list? lis) (values lis lis)	; Use NOT-PAIR? to handle dotted lists.
	(let ((elt (car lis))
	      (tail (cdr lis)))
	  (receive (in out) (recur tail)
	    (if (pred elt)
		(values (if (pair? out) (cons elt in) lis) out)
		(values in (if (pair? in) (cons elt out) lis))))))))



;(define (partition! pred lis)			; Things are much simpler
;  (let recur ((lis lis))			; if you are willing to
;    (if (null-list? lis) (values lis lis)	; push N stack frames & do N
;        (let ((elt (car lis)))			; SET-CDR! writes, where N is
;          (receive (in out) (recur (cdr lis))	; the length of LIS.
;            (cond ((pred elt)
;                   (set-cdr! lis in)
;                   (values lis out))
;                  (else (set-cdr! lis out)
;                        (values in lis))))))))


;;; This implementation of PARTITION!
;;; - doesn't cons, and uses no stack;
;;; - is careful not to do redundant SET-CDR! writes, as writes to memory are
;;;   usually expensive on modern machines, and can be extremely expensive on
;;;   modern Schemes (e.g., ones that have generational GC's).
;;; It just zips down contiguous runs of in and out elts in LIS doing the
;;; minimal number of SET-CDR!s to splice these runs together into the result
;;; lists.

(define (partition! pred lis)
  (check-arg procedure? pred partition!)
  (if (null-list? lis) (values lis lis)

      ;; This pair of loops zips down contiguous in & out runs of the
      ;; list, splicing the runs together. The invariants are
      ;;   SCAN-IN:  (cdr in-prev)  = LIS.
      ;;   SCAN-OUT: (cdr out-prev) = LIS.
      (letrec ((scan-in (lambda (in-prev out-prev lis)
			  (let lp ((in-prev in-prev) (lis lis))
			    (if (pair? lis)
				(if (pred (car lis))
				    (lp lis (cdr lis))
				    (begin (set-cdr! out-prev lis)
					   (scan-out in-prev lis (cdr lis))))
				(set-cdr! out-prev lis))))) ; Done.

	       (scan-out (lambda (in-prev out-prev lis)
			   (let lp ((out-prev out-prev) (lis lis))
			     (if (pair? lis)
				 (if (pred (car lis))
				     (begin (set-cdr! in-prev lis)
					    (scan-in lis out-prev (cdr lis)))
				     (lp lis (cdr lis)))
				 (set-cdr! in-prev lis)))))) ; Done.

	;; Crank up the scan&splice loops.
	(if (pred (car lis))
	    ;; LIS begins in-list. Search for out-list's first pair.
	    (let lp ((prev-l lis) (l (cdr lis)))
	      (cond ((not (pair? l)) (values lis l))
		    ((pred (car l)) (lp l (cdr l)))
		    (else (scan-out prev-l l (cdr l))
			  (values lis l))))	; Done.

	    ;; LIS begins out-list. Search for in-list's first pair.
	    (let lp ((prev-l lis) (l (cdr lis)))
	      (cond ((not (pair? l)) (values l lis))
		    ((pred (car l))
		     (scan-in l prev-l (cdr l))
		     (values l lis))		; Done.
		    (else (lp l (cdr l)))))))))


;;; Inline us, please.
(define (remove  pred l) (filter  (lambda (x) (not (pred x))) l))
(define (remove! pred l) (filter! (lambda (x) (not (pred x))) l))



;;; Here's the taxonomy for the DELETE/ASSOC/MEMBER functions.
;;; (I don't actually think these are the world's most important
;;; functions -- the procedural FILTER/REMOVE/FIND/FIND-TAIL variants
;;; are far more general.)
;;;
;;; Function			Action
;;; ---------------------------------------------------------------------------
;;; remove pred lis		Delete by general predicate
;;; delete x lis [=]		Delete by element comparison
;;;
;;; find pred lis		Search by general predicate
;;; find-tail pred lis		Search by general predicate
;;; member x lis [=]		Search by element comparison
;;;
;;; assoc key lis [=]		Search alist by key comparison
;;; alist-delete key alist [=]	Alist-delete by key comparison

(define (delete x lis . maybe-=)
  (let ((= (:optional maybe-= equal?)))
    (filter (lambda (y) (not (= x y))) lis)))

(define (delete! x lis . maybe-=)
  (let ((= (:optional maybe-= equal?)))
    (filter! (lambda (y) (not (= x y))) lis)))

; member is re-exported from (scheme base); the base binding takes
; the same optional comparison argument.

;;; right-duplicate deletion
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; delete-duplicates delete-duplicates!
;;;
;;; Beware -- these are N^2 algorithms. To efficiently remove duplicates
;;; in long lists, sort the list to bring duplicates together, then use a
;;; linear-time algorithm to kill the dups. Or use an algorithm based on
;;; element-marking. The former gives you O(n lg n), the latter is linear.

(define (delete-duplicates lis . maybe-=)
  (let ((elt= (:optional maybe-= equal?)))
    (check-arg procedure? elt= delete-duplicates)
    (let recur ((lis lis))
      (if (null-list? lis) lis
	  (let* ((x (car lis))
		 (tail (cdr lis))
		 (new-tail (recur (delete x tail elt=))))
	    (if (eq? tail new-tail) lis (cons x new-tail)))))))

(define (delete-duplicates! lis . maybe-=)
  (let ((elt= (:optional maybe-= equal?)))
    (check-arg procedure? elt= delete-duplicates!)
    (let recur ((lis lis))
      (if (null-list? lis) lis
	  (let* ((x (car lis))
		 (tail (cdr lis))
		 (new-tail (recur (delete! x tail elt=))))
	    (if (eq? tail new-tail) lis (cons x new-tail)))))))


;;; alist stuff
;;;;;;;;;;;;;;;

; assoc is re-exported from (scheme base); the base binding takes the
; same optional comparison argument.

(define (alist-cons key datum alist) (cons (cons key datum) alist))

(define (alist-copy alist)
  (map (lambda (elt) (cons (car elt) (cdr elt)))
       alist))

(define (alist-delete key alist . maybe-=)
  (let ((= (:optional maybe-= equal?)))
    (filter (lambda (elt) (not (= key (car elt)))) alist)))

(define (alist-delete! key alist . maybe-=)
  (let ((= (:optional maybe-= equal?)))
    (filter! (lambda (elt) (not (= key (car elt)))) alist)))


;;; find find-tail take-while drop-while span break any every list-index
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (find pred list)
  (cond ((find-tail pred list) => car)
	(else #f)))

(define (find-tail pred list)
  (check-arg procedure? pred find-tail)
  (let lp ((list list))
    (and (not (null-list? list))
	 (if (pred (car list)) list
	     (lp (cdr list))))))

(define (take-while pred lis)
  (check-arg procedure? pred take-while)
  (let recur ((lis lis))
    (if (null-list? lis) '()
	(let ((x (car lis)))
	  (if (pred x)
	      (cons x (recur (cdr lis)))
	      '())))))

(define (drop-while pred lis)
  (check-arg procedure? pred drop-while)
  (let lp ((lis lis))
    (if (null-list? lis) '()
	(if (pred (car lis))
	    (lp (cdr lis))
	    lis))))

(define (take-while! pred lis)
  (check-arg procedure? pred take-while!)
  (if (or (null-list? lis) (not (pred (car lis)))) '()
      (begin (let lp ((prev lis) (rest (cdr lis)))
	       (if (pair? rest)
		   (let ((x (car rest)))
		     (if (pred x) (lp rest (cdr rest))
			 (set-cdr! prev '())))))
	     lis)))

(define (span pred lis)
  (check-arg procedure? pred span)
  (let recur ((lis lis))
    (if (null-list? lis) (values '() '())
	(let ((x (car lis)))
	  (if (pred x)
	      (receive (prefix suffix) (recur (cdr lis))
		(values (cons x prefix) suffix))
	      (values '() lis))))))

(define (span! pred lis)
  (check-arg procedure? pred span!)
  (if (or (null-list? lis) (not (pred (car lis)))) (values '() lis)
      (let ((suffix (let lp ((prev lis) (rest (cdr lis)))
		      (if (null-list? rest) rest
			  (let ((x (car rest)))
			    (if (pred x) (lp rest (cdr rest))
				(begin (set-cdr! prev '())
				       rest)))))))
	(values lis suffix))))


(define (break  pred lis) (span  (lambda (x) (not (pred x))) lis))
(define (break! pred lis) (span! (lambda (x) (not (pred x))) lis))

(define (any pred lis1 . lists)
  (check-arg procedure? pred any)
  (if (pair? lists)

      ;; N-ary case
      (receive (heads tails) (%cars+cdrs (cons lis1 lists))
	(and (pair? heads)
	     (let lp ((heads heads) (tails tails))
	       (receive (next-heads next-tails) (%cars+cdrs tails)
		 (if (pair? next-heads)
		     (or (apply pred heads) (lp next-heads next-tails))
		     (apply pred heads)))))) ; Last PRED app is tail call.

      ;; Fast path
      (and (not (null-list? lis1))
	   (let lp ((head (car lis1)) (tail (cdr lis1)))
	     (if (null-list? tail)
		 (pred head)		; Last PRED app is tail call.
		 (or (pred head) (lp (car tail) (cdr tail))))))))


;(define (every pred list)              ; Simple definition.
;  (let lp ((list list))                ; Doesn't return the last PRED value.
;    (or (not (pair? list))
;        (and (pred (car list))
;             (lp (cdr list))))))

(define (every pred lis1 . lists)
  (check-arg procedure? pred every)
  (if (pair? lists)

      ;; N-ary case
      (receive (heads tails) (%cars+cdrs (cons lis1 lists))
	(or (not (pair? heads))
	    (let lp ((heads heads) (tails tails))
	      (receive (next-heads next-tails) (%cars+cdrs tails)
		(if (pair? next-heads)
		    (and (apply pred heads) (lp next-heads next-tails))
		    (apply pred heads)))))) ; Last PRED app is tail call.

      ;; Fast path
      (or (null-list? lis1)
	  (let lp ((head (car lis1))  (tail (cdr lis1)))
	    (if (null-list? tail)
		(pred head)	; Last PRED app is tail call.
		(and (pred head) (lp (car tail) (cdr tail))))))))

(define (list-index pred lis1 . lists)
  (check-arg procedure? pred list-index)
  (if (pair? lists)

      ;; N-ary case
      (let lp ((lists (cons lis1 lists)) (n 0))
	(receive (heads tails) (%cars+cdrs lists)
	  (and (pair? heads)
	       (if (apply pred heads) n
		   (lp tails (+ n 1))))))

      ;; Fast path
      (let lp ((lis lis1) (n 0))
	(and (not (null-list? lis))
	     (if (pred (car lis)) n (lp (cdr lis) (+ n 1)))))))

;;; Reverse
;;;;;;;;;;;

;R4RS, so not defined here.
;(define (reverse lis) (fold cons '() lis))

;(define (reverse! lis)
;  (pair-fold (lambda (pair tail) (set-cdr! pair tail) pair) '() lis))

(define (reverse! lis)
  (let lp ((lis lis) (ans '()))
    (if (null-list? lis) ans
        (let ((tail (cdr lis)))
          (set-cdr! lis ans)
          (lp tail lis)))))

;;; Lists-as-sets
;;;;;;;;;;;;;;;;;

;;; This is carefully tuned code; do not modify casually.
;;; - It is careful to share storage when possible;
;;; - Side-effecting code tries not to perform redundant writes.
;;; - It tries to avoid linear-time scans in special cases where constant-time
;;;   computations can be performed.
;;; - It relies on similar properties from the other list-lib procs it calls.
;;;   For example, it uses the fact that the implementations of MEMBER and
;;;   FILTER in this source code share longest common tails between args
;;;   and results to get structure sharing in the lset procedures.

(define (%lset2<= = lis1 lis2) (every (lambda (x) (member x lis2 =)) lis1))

(define (lset<= = . lists)
  (check-arg procedure? = lset<=)
  (or (not (pair? lists)) ; 0-ary case
      (let lp ((s1 (car lists)) (rest (cdr lists)))
	(or (not (pair? rest))
	    (let ((s2 (car rest))  (rest (cdr rest)))
	      (and (or (eq? s2 s1)	; Fast path
		       (%lset2<= = s1 s2)) ; Real test
		   (lp s2 rest)))))))

(define (lset= = . lists)
  (define (flip proc) (lambda (x y) (proc y x)))
  (check-arg procedure? = lset=)
  (or (not (pair? lists)) ; 0-ary case
      (let lp ((s1 (car lists)) (rest (cdr lists)))
        (or (not (pair? rest))
            (let ((s2   (car rest))
                  (rest (cdr rest)))
              (and (or (eq? s1 s2)            ; Fast path
                       (and (%lset2<= = s1 s2) ; Real test
                            (%lset2<= (flip =) s2 s1)))
                   (lp s2 rest)))))))

(define (lset-adjoin = lis . elts)
  (check-arg procedure? = lset-adjoin)
  (fold (lambda (elt ans) (if (member elt ans =) ans (cons elt ans)))
	lis elts))


(define (lset-union = . lists)
  (check-arg procedure? = lset-union)
  (reduce (lambda (lis ans)		; Compute ANS + LIS.
	    (cond ((null? lis) ans)	; Don't copy any lists
		  ((null? ans) lis)	; if we don't have to.
		  ((eq? lis ans) ans)
		  (else
		   (fold (lambda (elt ans) (if (any (lambda (x) (= x elt)) ans)
					       ans
					       (cons elt ans)))
			 ans lis))))
	  '() lists))

(define (lset-union! = . lists)
  (check-arg procedure? = lset-union!)
  (reduce (lambda (lis ans)		; Splice new elts of LIS onto the front of ANS.
	    (cond ((null? lis) ans)	; Don't copy any lists
		  ((null? ans) lis)	; if we don't have to.
		  ((eq? lis ans) ans)
		  (else
		   (pair-fold (lambda (pair ans)
				(let ((elt (car pair)))
				  (if (any (lambda (x) (= x elt)) ans)
				      ans
				      (begin (set-cdr! pair ans) pair))))
			      ans lis))))
	  '() lists))


(define (lset-intersection = lis1 . lists)
  (check-arg procedure? = lset-intersection)
  (let ((lists (delete lis1 lists eq?))) ; Throw out any LIS1 vals.
    (cond ((any null-list? lists) '())		; Short cut
	  ((null? lists)          lis1)		; Short cut
	  (else (filter (lambda (x)
			  (every (lambda (lis) (member x lis =)) lists))
			lis1)))))

(define (lset-intersection! = lis1 . lists)
  (check-arg procedure? = lset-intersection!)
  (let ((lists (delete lis1 lists eq?))) ; Throw out any LIS1 vals.
    (cond ((any null-list? lists) '())		; Short cut
	  ((null? lists)          lis1)		; Short cut
	  (else (filter! (lambda (x)
			   (every (lambda (lis) (member x lis =)) lists))
			 lis1)))))


(define (lset-difference = lis1 . lists)
  (check-arg procedure? = lset-difference)
  (let ((lists (filter pair? lists)))	; Throw out empty lists.
    (cond ((null? lists)     lis1)	; Short cut
	  ((memq lis1 lists) '())	; Short cut
	  (else (filter (lambda (x)
			  (every (lambda (lis) (not (member x lis =)))
				 lists))
			lis1)))))

(define (lset-difference! = lis1 . lists)
  (check-arg procedure? = lset-difference!)
  (let ((lists (filter pair? lists)))	; Throw out empty lists.
    (cond ((null? lists)     lis1)	; Short cut
	  ((memq lis1 lists) '())	; Short cut
	  (else (filter! (lambda (x)
			   (every (lambda (lis) (not (member x lis =)))
				  lists))
			 lis1)))))


(define (lset-xor = . lists)
  (check-arg procedure? = lset-xor)
  (reduce (lambda (b a)			; Compute A xor B:
	    ;; Note that this code relies on the constant-time
	    ;; short-cuts provided by LSET-DIFF+INTERSECTION,
	    ;; LSET-DIFFERENCE & APPEND to provide constant-time short
	    ;; cuts for the cases A = (), B = (), and A eq? B. It takes
	    ;; a careful case analysis to see it, but it's carefully
	    ;; built in.

	    ;; Compute a-b and a^b, then compute b-(a^b) and
	    ;; cons it onto the front of a-b.
	    (receive (a-b a-int-b)   (lset-diff+intersection = a b)
	      (cond ((null? a-b)     (lset-difference = b a))
		    ((null? a-int-b) (append b a))
		    (else (fold (lambda (xb ans)
				  (if (member xb a-int-b =) ans (cons xb ans)))
				a-b
				b)))))
	  '() lists))


(define (lset-xor! = . lists)
  (check-arg procedure? = lset-xor!)
  (reduce (lambda (b a)			; Compute A xor B:
	    ;; Note that this code relies on the constant-time
	    ;; short-cuts provided by LSET-DIFF+INTERSECTION,
	    ;; LSET-DIFFERENCE & APPEND to provide constant-time short
	    ;; cuts for the cases A = (), B = (), and A eq? B. It takes
	    ;; a careful case analysis to see it, but it's carefully
	    ;; built in.

	    ;; Compute a-b and a^b, then compute b-(a^b) and
	    ;; cons it onto the front of a-b.
	    (receive (a-b a-int-b)   (lset-diff+intersection! = a b)
	      (cond ((null? a-b)     (lset-difference! = b a))
		    ((null? a-int-b) (append! b a))
		    (else (pair-fold (lambda (b-pair ans)
				       (if (member (car b-pair) a-int-b =) ans
					   (begin (set-cdr! b-pair ans) b-pair)))
				     a-b
				     b)))))
	  '() lists))


(define (lset-diff+intersection = lis1 . lists)
  (check-arg procedure? = lset-diff+intersection)
  (cond ((every null-list? lists) (values lis1 '()))	; Short cut
	((memq lis1 lists)        (values '() lis1))	; Short cut
	(else (partition (lambda (elt)
			   (not (any (lambda (lis) (member elt lis =))
				     lists)))
			 lis1))))

(define (lset-diff+intersection! = lis1 . lists)
  (check-arg procedure? = lset-diff+intersection!)
  (cond ((every null-list? lists) (values lis1 '()))	; Short cut
	((memq lis1 lists)        (values '() lis1))	; Short cut
	(else (partition! (lambda (elt)
			    (not (any (lambda (lis) (member elt lis =))
				      lists)))
			  lis1))))
