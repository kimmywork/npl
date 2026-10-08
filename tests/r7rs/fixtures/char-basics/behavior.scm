; (scheme char) over ASCII plus base char/integer conversion (R7RS 6.6).
; Out of scope until the library stage: non-ASCII characters (the runtime
; character repertoire is ASCII) and catchable primitive type errors (native
; primitives currently terminate the process).
(define (report name value) (write (list name value)) (newline))
(define (char-results)
  (report 'convert (list (char->integer #\a) (integer->char 66)
                         (char->integer (integer->char 126)) (char? #\x)
                         (char? 1)))
  (report 'case (list (char-upcase #\a) (char-downcase #\Q) (char-foldcase #\Z)
                      (char-upcase #\1)))
  (report 'classes (list (char-alphabetic? #\a) (char-alphabetic? #\1)
                         (char-numeric? #\7) (char-whitespace? #\space)
                         (char-whitespace? #\a) (char-upper-case? #\A)
                         (char-lower-case? #\A)))
  (report 'digits (list (digit-value #\7) (digit-value #\a)))
  (report 'compare (list (char=? #\a #\a #\a) (char<? #\a #\b #\c)
                         (char<? #\a #\c #\b) (char>=? #\b #\b #\a)
                         (char-ci=? #\a #\A) (char-ci<? #\a #\B))))
(define fixture-result (begin (char-results) 0))
