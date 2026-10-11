; (scheme base) list, equivalence, and boolean/symbol procedures (R7RS 6.1,
; 6.3, 6.4, 6.5). Shared by native code and the evaluator through the library.

(define (caar x) (car (car x)))
(define (cadr x) (car (cdr x)))
(define (cdar x) (cdr (car x)))
(define (cddr x) (cdr (cdr x)))

(define (boolean? value) (if (eq? value #t) #t (eq? value #f)))

(define (all-adjacent? same? first rest)
  (if (null? rest)
      #t
      (if (same? first (car rest))
          (all-adjacent? same? (car rest) (cdr rest))
          #f)))

(define (check-all who predicate values)
  (if (pair? values)
      (begin
        (if (not (predicate (car values)))
            (argument-error who "wrong argument type" (car values)))
        (check-all who predicate (cdr values)))))

(define (boolean=? first second . rest)
  (check-all "boolean=?" boolean? (cons first (cons second rest)))
  (all-adjacent? eq? first (cons second rest)))

(define (symbol=? first second . rest)
  (check-all "symbol=?" symbol? (cons first (cons second rest)))
  (all-adjacent? eq? first (cons second rest)))

; ---------------- traversal ----------------

(define (any-null? lists)
  (if (null? lists) #f (if (null? (car lists)) #t (any-null? (cdr lists)))))

(define (cars lists)
  (if (null? lists) '() (cons (car (car lists)) (cars (cdr lists)))))

(define (cdrs lists)
  (if (null? lists) '() (cons (cdr (car lists)) (cdrs (cdr lists)))))

; Results are accumulated in order; map stops at the shortest list (R7RS).
(define (map-one procedure items output)
  (if (null? items)
      (reverse output)
      (map-one procedure (cdr items) (cons (procedure (car items)) output))))

(define (map-many procedure lists output)
  (if (any-null? lists)
      (reverse output)
      (map-many procedure (cdrs lists)
                (cons (apply procedure (cars lists)) output))))

(define (map procedure first . rest)
  (if (null? rest)
      (map-one procedure first '())
      (map-many procedure (cons first rest) '())))

(define (for-each-one procedure items)
  (if (pair? items)
      (begin (procedure (car items)) (for-each-one procedure (cdr items)))))

(define (for-each-many procedure lists)
  (if (not (any-null? lists))
      (begin
        (apply procedure (cars lists))
        (for-each-many procedure (cdrs lists)))))

(define (for-each procedure first . rest)
  (if (null? rest)
      (for-each-one procedure first)
      (for-each-many procedure (cons first rest))))

; ---------------- construction and access ----------------

(define (make-list-loop count fill output)
  (if (raw-equal count 0)
      output
      (make-list-loop (raw-subtract count 1) fill (cons fill output))))

(define (make-list count . fill)
  (if (not (if (exact-integer? count) (raw-less-equal 0 count) #f))
      (argument-error "make-list" "expected a non-negative exact count" count))
  (make-list-loop count (if (pair? fill) (car fill) #f) '()))

; The error names the caller and the index it was given.
(define (checked-list-tail who items index)
  (let loop ((rest items) (count index))
    (if (raw-equal count 0)
        rest
        (if (pair? rest)
            (loop (cdr rest) (raw-subtract count 1))
            (argument-error who "index out of range" index)))))

(define (list-tail items count) (checked-list-tail "list-tail" items count))

(define (list-ref items index)
  (let ((tail (checked-list-tail "list-ref" items index)))
    (if (pair? tail) (car tail)
        (argument-error "list-ref" "index out of range" index))))

(define (list-set! items index value)
  (let ((tail (checked-list-tail "list-set!" items index)))
    (if (pair? tail)
        (set-car! tail value)
        (argument-error "list-set!" "index out of range" index))))

; Copies the spine, keeping an improper tail (R7RS 6.4).
(define (list-copy items)
  (if (pair? items) (cons (car items) (list-copy (cdr items))) items))

; R7RS 6.4: any number of lists; the last argument is shared, not copied,
; and may be any object.
(define (append . lists)
  (if (null? lists)
      '()
      (if (null? (cdr lists))
          (car lists)
          (raw-append (car lists) (apply append (cdr lists))))))

; ---------------- membership ----------------

(define (member-loop value items same?)
  (if (pair? items)
      (if (same? value (car items))
          items
          (member-loop value (cdr items) same?))
      #f))

(define (memq value items) (member-loop value items eq?))
(define (memv value items) (member-loop value items eqv?))
(define (member value items . compare)
  (member-loop value items (if (pair? compare) (car compare) equal?)))

(define (assoc-loop value entries same?)
  (if (pair? entries)
      (if (same? value (car (car entries)))
          (car entries)
          (assoc-loop value (cdr entries) same?))
      #f))

(define (assq value entries) (assoc-loop value entries eq?))
(define (assv value entries) (assoc-loop value entries eqv?))
(define (assoc value entries . compare)
  (assoc-loop value entries (if (pair? compare) (car compare) equal?)))

; ---------------- equal? ----------------
; Structural equality on pairs, vectors, strings, and bytevectors; eqv?
; elsewhere (R7RS 6.1). It must terminate on circular data. The direct pass
; walks cdr chains iteratively and watches the left chain for a cycle
; (tortoise and hare); every other cycle passes through a car or vector
; element, so a bound on that nesting depth catches it. When either check
; fires, the comparison restarts with union-find over the visited pairs and
; vectors: two nodes already in one class are taken as equal, which decides
; equality of the infinite trees (Adams and Dybvig, "Efficient
; nondestructive equality checking for trees and graphs"). Acyclic data of
; ordinary depth never leaves the direct pass, whatever its length.

(define (equal-atoms? left right)
  (cond
    ((string? left) (if (string? right) (string=? left right) #f))
    ((bytevector? left)
     (if (bytevector? right)
         (if (raw-equal (bytevector-length left) (bytevector-length right))
             (bytevector-equal? left right 0 (bytevector-length left))
             #f)
         #f))
    (else #f)))

(define (bytevector-equal? left right index length)
  (if (raw-equal index length)
      #t
      (if (raw-equal (bytevector-u8-ref left index)
                     (bytevector-u8-ref right index))
          (bytevector-equal? left right (raw-add index 1) length)
          #f)))

;; Direct pass: #t, #f, or 'cycle (give up; use the graph pass).
(define (equal-direct left right depth)
  (cond
    ((eqv? left right) #t)
    ((raw-greater depth 10000) 'cycle)
    ((pair? left)
     (if (pair? right) (equal-direct-list left right depth) #f))
    ((vector? left)
     (if (if (vector? right)
             (raw-equal (vector-length left) (vector-length right))
             #f)
         (let loop ((index 0))
           (if (raw-equal index (vector-length left))
               #t
               (let ((element (equal-direct (vector-ref left index)
                                            (vector-ref right index)
                                            (raw-add depth 1))))
                 (if (eq? element #t) (loop (raw-add index 1)) element))))
         #f))
    (else (equal-atoms? left right))))

; Both arguments are pairs. tortoise advances every second step along the
; left chain; meeting it again means the left cdr chain is circular.
(define (equal-direct-list left right depth)
  (let loop ((left left) (right right) (tortoise left) (step #f))
    (let ((head (equal-direct (car left) (car right) (raw-add depth 1))))
      (if (not (eq? head #t))
          head
          (let ((left-rest (cdr left)) (right-rest (cdr right))
                (tortoise (if step (cdr tortoise) tortoise)))
            (cond
              ((eq? left-rest tortoise) 'cycle)
              ((pair? left-rest)
               (if (pair? right-rest)
                   (loop left-rest right-rest tortoise (not step))
                   #f))
              (else (equal-direct left-rest right-rest depth))))))))

; Union-find over nodes (pairs and vectors), keyed by eq?: an association
; list from node to its parent cell; small, because only the graph part
; that defeated the bounded pass is visited.
(define (equal-find classes node)
  (let ((entry (assq node (car classes))))
    (if entry
        (let ((root (equal-find classes (cdr entry))))
          (set-cdr! entry root)
          root)
        node)))

(define (equal-union! classes a b)
  (let ((ra (equal-find classes a)) (rb (equal-find classes b)))
    (if (eq? ra rb)
        #t
        (begin (set-car! classes (cons (cons ra rb) (car classes))) #f))))

(define (equal-graph? left right classes)
  (cond
    ((eqv? left right) #t)
    ((pair? left)
     (if (pair? right)
         (let loop ((left left) (right right))
           (cond
             ((equal-union! classes left right) #t)
             ((not (equal-graph? (car left) (car right) classes)) #f)
             ((if (pair? (cdr left)) (pair? (cdr right)) #f)
              (loop (cdr left) (cdr right)))
             (else (equal-graph? (cdr left) (cdr right) classes))))
         #f))
    ((vector? left)
     (if (if (vector? right)
             (raw-equal (vector-length left) (vector-length right))
             #f)
         (if (equal-union! classes left right)
             #t
             (let loop ((index 0))
               (if (raw-equal index (vector-length left))
                   #t
                   (if (equal-graph? (vector-ref left index)
                                     (vector-ref right index) classes)
                       (loop (raw-add index 1))
                       #f))))
         #f))
    (else (equal-atoms? left right))))

(define (equal? left right)
  (let ((result (equal-direct left right 0)))
    (if (eq? result 'cycle)
        (equal-graph? left right (list '()))
        result)))
