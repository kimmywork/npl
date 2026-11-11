; (scheme char) over Unicode (R7RS 6.6, 6.7): properties, digit values,
; simple character mappings, and full string mappings with Final_Sigma.
; Cases where Guile 3.0 and Racket disagree are left out (see
; .agents/archive/design/design-l6.md): string-foldcase of capital sharp s and final sigma,
; and the Other_Alphabetic/Other_Uppercase properties of letterlike numbers
; and symbols.
(define (report name value) (write (list name value)) (newline))
(define (code n) (integer->char n))
(define (props c)
  (list (char-alphabetic? c) (char-numeric? c) (char-whitespace? c)
        (char-upper-case? c) (char-lower-case? c) (digit-value c)))

(define r1 (report 'ascii (map props (list #\A #\z #\5 #\space #\! #\newline))))
(define r2 (report 'letters
  (map props (list (code #xE9) (code #x3BB) (code #x3A3) (code #x10400)
                   (code #x4E2D) (code #x1C5)))))
(define r3 (report 'digits
  (map (lambda (n) (list (char-numeric? (code n)) (digit-value (code n))))
       (list #x660 #x966 #xFF10 #x1D7CE #xBD #x2160))))
(define r4 (report 'whitespace
  (map char-whitespace?
       (list (code #xA0) (code #x2028) (code #x3000) (code #x200B) #\tab))))
(define r5 (report 'char-case
  (map (lambda (n)
         (let ((c (code n)))
           (map char->integer
                (list (char-upcase c) (char-downcase c) (char-foldcase c)))))
       (list #x61 #xDF #x130 #x131 #x3C2 #x1E9E #x1C5 #x10428))))
(define r6 (report 'string-upcase
  (list (string-upcase "straße") (string-upcase "ﬁx") (string-upcase "ǆ")
        (string-upcase "λόγος") (string-upcase "ŉ"))))
(define r7 (report 'string-downcase
  (list (string-downcase "ΣΑΣ") (string-downcase "ΑΣ Σ ΑΣ.")
        (string-downcase "ΌΔΟΣ'") (string-downcase "İ") (string-downcase "ǅ"))))
(define r8 (report 'string-foldcase
  (list (string-foldcase "Straße") (string-foldcase "İ") (string-foldcase "ΣΑ"))))
(define r9 (report 'ci-comparison
  (list (char-ci=? #\a #\A (code #x24B6) (code #x24D0))
        (char-ci=? (code #x3C3) (code #x3A3) (code #x3C2))
        (char-ci<? #\a #\B #\c)
        (string-ci=? "ΛΌΓΟΣ" "λόγοσ")
        (string-ci<? "apple" "BANANA"))))
