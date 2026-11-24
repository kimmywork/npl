; Results R7RS leaves unspecified: one-armed conditionals, set!, mutators,
; output procedures, and do without result expressions. NPL returns one
; unspecified value (runtime IMM_UNSPECIFIED) that writes as #<unspecified>,
; as Guile does; the REPL prints nothing for it.
(define (show x) (write x) (newline))
(define v (vector 1 2))
(define p (list 1 2))
(define s (make-string 2 #\a))
(define counter 0)
(define r1 (show (list (if #f #f) (when #f 1) (unless #t 1))))
(define r2 (show (list (cond (#f 1)) (case 3 ((1) 'one)))))
(define r3 (show (list (vector-set! v 0 9) (set-car! p 9) (string-set! s 0 #\b)
                       v p s)))
(define r4 (show (list (set! counter 1) counter)))
(define r5 (show (list (for-each (lambda (x) x) '(1 2)) (vector-fill! v 0))))
(define r6 (show (list (newline (open-output-string))
                       (display "" (open-output-string))
                       (write-char #\a (open-output-string)))))
(define r7 (show (do ((i 0 (+ i 1))) ((= i 2)))))
(define r8 (show (eq? (if #f #f) (vector-set! v 1 0))))
; The unspecified value is true, like every value except #f.
(define r9 (show (if (if #f #f) 'true 'false)))
