; (scheme base) characters and strings (R7RS 6.6, 6.7). Strings hold Unicode
; scalar values (runtime/string.c); the UTF-8 conversions are runtime
; primitives.

; ---------------- characters ----------------

(define (char-codes who characters)
  (map (lambda (c)
         (if (char? c) (char->integer c)
             (argument-error who "expected a character" c)))
       characters))

(define (codes-ordered? same-or-ordered? codes)
  (if (null? (cdr codes))
      #t
      (if (same-or-ordered? (car codes) (car (cdr codes)))
          (codes-ordered? same-or-ordered? (cdr codes))
          #f)))

(define (char=? a b . rest)
  (codes-ordered? raw-equal (char-codes "char=?" (cons a (cons b rest)))))
(define (char<? a b . rest)
  (codes-ordered? raw-less (char-codes "char<?" (cons a (cons b rest)))))
(define (char>? a b . rest)
  (codes-ordered? raw-greater (char-codes "char>?" (cons a (cons b rest)))))
(define (char<=? a b . rest)
  (codes-ordered? raw-less-equal
                  (char-codes "char<=?" (cons a (cons b rest)))))
(define (char>=? a b . rest)
  (codes-ordered? raw-greater-equal
                  (char-codes "char>=?" (cons a (cons b rest)))))

; ---------------- construction and conversion ----------------

(define (make-string count . fill)
  (npl_make_string count (if (pair? fill) (car fill) #\space)))

(define (list->string characters)
  (let ((result (make-string (length characters))))
    (let loop ((index 0) (rest characters))
      (if (pair? rest)
          (begin
            (string-set! result index (car rest))
            (loop (raw-add index 1) (cdr rest)))))
    result))

(define (string . characters) (list->string characters))

(define (string-slice->list source start end output)
  (if (raw-equal start end)
      output
      (string-slice->list source start (raw-subtract end 1)
        (cons (string-ref source (raw-subtract end 1)) output))))

(define (string->list source . range)
  (let ((bounds (checked-range "string->list" (string-length source) range)))
    (string-slice->list source (car bounds) (cdr bounds) '())))

(define (string-copy source . range)
  (list->string (apply string->list source range)))

(define (string-copy! target at source . range)
  (let ((characters (apply string->list source range)))
    (if (if (exact-integer? at)
            (if (raw-less-equal 0 at)
                (raw-less-equal (raw-add at (length characters))
                                (string-length target))
                #f)
            #f)
        (let loop ((index at) (rest characters))
          (if (pair? rest)
              (begin
                (string-set! target index (car rest))
                (loop (raw-add index 1) (cdr rest)))))
        (argument-error "string-copy!" "index out of range" at))))

(define (string-fill! target fill . range)
  (let ((bounds (checked-range "string-fill!" (string-length target) range)))
    (npl_string_fill target fill (car bounds) (cdr bounds))))

(define (vector->string source . range)
  (list->string (apply vector->list source range)))

(define (string->vector source . range)
  (list->vector (apply string->list source range)))

(define (string->utf8 source . range)
  (let ((bounds (checked-range "string->utf8" (string-length source) range)))
    (npl_string_to_utf8 source (car bounds) (cdr bounds))))

(define (utf8->string source . range)
  (let ((bounds (checked-range "utf8->string" (bytevector-length source)
                               range)))
    (npl_utf8_to_string source (car bounds) (cdr bounds))))

; ---------------- comparison ----------------

; Lexicographic order on character codes; a proper prefix sorts first.
(define (string-compare left right)
  (let loop ((index 0))
    (cond
      ((raw-equal index (string-length left))
       (if (raw-equal index (string-length right)) 0 -1))
      ((raw-equal index (string-length right)) 1)
      (else
       (let ((a (char->integer (string-ref left index)))
             (b (char->integer (string-ref right index))))
         (cond ((raw-less a b) -1)
               ((raw-less b a) 1)
               (else (loop (raw-add index 1)))))))))

(define (strings-ordered? who accept? strings)
  (for-each (lambda (s)
              (if (not (string? s))
                  (argument-error who "expected a string" s)))
            strings)
  (let loop ((rest strings))
    (if (null? (cdr rest))
        #t
        (if (accept? (string-compare (car rest) (car (cdr rest))))
            (loop (cdr rest))
            #f))))

(define (string=? a b . rest)
  (strings-ordered? "string=?" (lambda (o) (raw-equal o 0))
                    (cons a (cons b rest))))
(define (string<? a b . rest)
  (strings-ordered? "string<?" (lambda (o) (raw-less o 0))
                    (cons a (cons b rest))))
(define (string>? a b . rest)
  (strings-ordered? "string>?" (lambda (o) (raw-greater o 0))
                    (cons a (cons b rest))))
(define (string<=? a b . rest)
  (strings-ordered? "string<=?" (lambda (o) (raw-less-equal o 0))
                    (cons a (cons b rest))))
(define (string>=? a b . rest)
  (strings-ordered? "string>=?" (lambda (o) (raw-greater-equal o 0))
                    (cons a (cons b rest))))

; ---------------- higher order ----------------

(define (string-map procedure first . rest)
  (list->string
    (apply map procedure (string->list first) (map string->list rest))))

(define (string-for-each procedure first . rest)
  (apply for-each procedure (string->list first) (map string->list rest)))
