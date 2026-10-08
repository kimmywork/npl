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
            (raise (string-append who ": wrong argument type")))
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
  (if (not (exact-integer? count)) (raise "make-list: expected an exact count"))
  (if (raw-less count 0) (raise "make-list: count must not be negative"))
  (make-list-loop count (if (pair? fill) (car fill) #f) '()))

(define (list-tail items count)
  (if (raw-equal count 0)
      items
      (if (pair? items)
          (list-tail (cdr items) (raw-subtract count 1))
          (raise "list-tail: index out of range"))))

(define (list-ref items index)
  (let ((tail (list-tail items index)))
    (if (pair? tail) (car tail) (raise "list-ref: index out of range"))))

(define (list-set! items index value)
  (let ((tail (list-tail items index)))
    (if (pair? tail)
        (set-car! tail value)
        (raise "list-set!: index out of range"))))

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

(define (vector-equal? left right index length)
  (if (raw-equal index length)
      #t
      (if (equal? (vector-ref left index) (vector-ref right index))
          (vector-equal? left right (raw-add index 1) length)
          #f)))

(define (bytevector-equal? left right index length)
  (if (raw-equal index length)
      #t
      (if (raw-equal (bytevector-u8-ref left index)
                     (bytevector-u8-ref right index))
          (bytevector-equal? left right (raw-add index 1) length)
          #f)))

; Structural equality on pairs, vectors, strings, and bytevectors; eqv?
; elsewhere (R7RS 6.1). Cycles are not detected.
(define (equal? left right)
  (cond
    ((eqv? left right) #t)
    ((pair? left)
     (if (pair? right)
         (if (equal? (car left) (car right))
             (equal? (cdr left) (cdr right))
             #f)
         #f))
    ((string? left) (if (string? right) (string=? left right) #f))
    ((vector? left)
     (if (vector? right)
         (if (raw-equal (vector-length left) (vector-length right))
             (vector-equal? left right 0 (vector-length left))
             #f)
         #f))
    ((bytevector? left)
     (if (bytevector? right)
         (if (raw-equal (bytevector-length left) (bytevector-length right))
             (bytevector-equal? left right 0 (bytevector-length left))
             #f)
         #f))
    (else #f)))
