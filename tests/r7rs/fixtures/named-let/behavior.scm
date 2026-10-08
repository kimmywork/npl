; Named let (R7RS 4.2.4): the name is bound in the body only, the inits are
; evaluated in the outer environment, and the loop is properly tail-recursive.
(define (report name value) (write (list name value)) (newline))

(define (sum-to n)
  (let loop ((i 0) (acc 0))
    (if (> i n) acc (loop (+ i 1) (+ acc i)))))

(define (reverse-list xs)
  (let rev ((rest xs) (out '()))
    (if (null? rest) out (rev (cdr rest) (cons (car rest) out)))))

(define (init-sees-outer)
  (let ((loop 10))
    (let loop ((i loop))
      (if (= i 0) 'done (loop (- i 1))))))

(define (name-shadows-outer)
  (let ((f (lambda (x) 'outer)))
    (let f ((n 2))
      (if (= n 0) 'inner (f (- n 1))))))

(define (no-bindings)
  (let once () 'ran))

(define (captured-loop)
  (let loop ((i 0) (fs '()))
    (if (= i 3)
        (let collect ((fs fs) (out '()))
          (if (null? fs) out (collect (cdr fs) (cons ((car fs)) out))))
        (loop (+ i 1) (cons (lambda () (* i 10)) fs)))))

(define (nested-named-lets)
  (let outer ((i 0) (acc '()))
    (if (= i 2)
        acc
        (outer (+ i 1)
               (let inner ((j 0) (acc acc))
                 (if (= j 2) acc (inner (+ j 1) (cons (list i j) acc))))))))

(define (internal-define-in-body)
  (let loop ((i 0))
    (define limit 3)
    (if (= i limit) i (loop (+ i 1)))))

(define (named-let-results)
  (report 'sum (sum-to 100))
  (report 'reverse (reverse-list '(1 2 3 4)))
  (report 'init-sees-outer (init-sees-outer))
  (report 'name-shadows-outer (name-shadows-outer))
  (report 'no-bindings (no-bindings))
  (report 'captured-loop (captured-loop))
  (report 'nested (nested-named-lets))
  (report 'internal-define (internal-define-in-body))
  (report (quote tail-loop) (let loop ((i 0)) (if (= i 20000) i (loop (+ i 1))))))
(define fixture-result (begin (named-let-results) 0))
