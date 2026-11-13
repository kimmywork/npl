; Shared helpers for the NPL string libraries. A library that includes this
; file gets its own private copy; the names are not exported.
;
; SRFI 13 accepts a character, a character set, or a predicate wherever a
; "char/char-set/pred" criterion appears. NPL has no character-set type yet,
; so a criterion is a character or a predicate; anything else is an error.

(define (%string-error who message . irritants)
  (apply error (string-append who ": " message) irritants))

; Parses the optional [start end] tail of a string procedure.
(define (%string-range who s args)
  (let* ((n (string-length s))
         (start (if (pair? args) (car args) 0))
         (rest (if (pair? args) (cdr args) '()))
         (end (if (pair? rest) (car rest) n)))
    (if (and (exact-integer? start) (exact-integer? end)
             (<= 0 start end n))
        (cons start end)
        (%string-error who "invalid range" start end))))

; Parses the optional [start1 end1 start2 end2] tail of a two-string
; procedure. Each bound defaults independently.
(define (%string-ranges who s1 s2 args)
  (let* ((n1 (string-length s1))
         (n2 (string-length s2))
         (start1 (if (pair? args) (car args) 0))
         (args (if (pair? args) (cdr args) '()))
         (end1 (if (pair? args) (car args) n1))
         (args (if (pair? args) (cdr args) '()))
         (start2 (if (pair? args) (car args) 0))
         (args (if (pair? args) (cdr args) '()))
         (end2 (if (pair? args) (car args) n2)))
    (if (and (exact-integer? start1) (exact-integer? end1)
             (exact-integer? start2) (exact-integer? end2)
             (<= 0 start1 end1 n1)
             (<= 0 start2 end2 n2))
        (list start1 end1 start2 end2)
        (%string-error who "invalid range"
                       start1 end1 start2 end2))))

(define (%string-criterion who criterion)
  (cond ((char? criterion)
         (lambda (c) (char=? c criterion)))
        ((procedure? criterion) criterion)
        (else
         (%string-error who
                        "expected a character or a predicate"
                        criterion))))

; A character or string, as a string; used by the unfold constructors.
(define (%string-part who value)
  (cond ((char? value) (string value))
        ((string? value) value)
        (else (%string-error who "expected a character or string" value))))

(define (%string-count-check who s n)
  (if (and (exact-integer? n) (<= 0 n (string-length s)))
      n
      (%string-error who "invalid count" n)))
