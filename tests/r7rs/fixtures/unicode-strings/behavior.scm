; Unicode characters and strings (R7RS 6.6, 6.7; .agents/archive/design/design-l6.md Part U):
; scalar-value characters, O(1) indexing at any width, mutation that widens
; a narrow string in place, UTF-8 conversion and ports, and write syntax.
(define (report name value) (write (list name value)) (newline))
(define (bytes bv)
  (let loop ((i (- (bytevector-length bv) 1)) (acc '()))
    (if (< i 0) acc (loop (- i 1) (cons (bytevector-u8-ref bv i) acc)))))
(define lambda-char (integer->char 955))
(define smile (integer->char #x1F600))
(define s (string #\a (integer->char 233) smile))

(define r1 (report 'characters
  (list (char->integer lambda-char) (char->integer smile) (char? smile)
        (char<? #\z lambda-char smile) (eqv? #\x3bb lambda-char))))
(define r2 (report 'scalar-range
  (list (guard (e ((error-object? e) 'error)) (integer->char #xD800))
        (guard (e ((error-object? e) 'error)) (integer->char #x110000))
        (char->integer (integer->char #x10FFFF)))))
(define r3 (report 'length-and-index
  (list (string-length s) (string-ref s 1) (string-ref s 2)
        (string-length "λx") (string-ref "λx" 0))))
(define r4 (report 'widening-mutation
  (let* ((m (make-string 3 #\x))
         (before m)
         (widened (begin (string-set! m 1 smile) (string-copy m)))
         (same (eq? before m))
         (length (string-length m))
         (char (string-ref m 1))
         (narrow-store (begin (string-set! m 0 #\y) (string-copy m))))
    (list widened same length char narrow-store))))
(define r5 (report 'construction
  (list (make-string 2 lambda-char) (string-append "a" s "b")
        (substring s 1 3) (string-copy s 2) (list->string (list lambda-char #\x))
        (string->list s) (string->vector s))))
(define r6 (report 'comparison
  (list (string=? s (string-copy s)) (string<? "a" "λ") (string<? "λ" "😀")
        (equal? s (string-append (string #\a) (string-copy s 1))))))
(define r7 (report 'fill-and-map
  (list (let ((m (string-copy "abcd"))) (string-fill! m lambda-char 1 3) m)
        (string-map (lambda (c) (if (char=? c #\a) lambda-char c)) "banana")
        (let ((n 0)) (string-for-each (lambda (c) (set! n (+ n (char->integer c)))) "λ😀") n))))
(define r8 (report 'utf8
  (list (bytes (string->utf8 s)) (bytes (string->utf8 s 1 2))
        (utf8->string (string->utf8 s)) (utf8->string (bytevector 206 187 120) 0 2)
        (guard (e ((error-object? e) 'invalid)) (utf8->string (bytevector 255))))))
(define r9 (report 'symbols
  (list (string->symbol "λx") (symbol->string 'λx) (eq? 'λx (string->symbol "λx"))
        (symbol->string (string->symbol (string smile))))))
(define r10 (report 'string-ports
  (let ((out (open-output-string)))
    (write-char lambda-char out)
    (display s out)
    (write-string "😀!" out)
    (let ((text (get-output-string out)))
      (let ((in (open-input-string text)))
        (let* ((a (read-char in)) (b (peek-char in)) (c (read-char in))
               (rest (read-string 10 in)))
          (list text (string-length text) a b c rest)))))))
(define r11 (report 'read-syntax
  (read (open-input-string "(\"\\x3bb;y\" #\\x3bb #\\λ λsym #\\nul #\\null)"))))
(define r12 (report 'write-syntax
  (let ((out (open-output-string)))
    (write (list lambda-char smile #\x7f #\x0 #\x7 (integer->char #x200B)
                 "é\x7;" (string (integer->char #x200B)) 'λ)
           out)
    (get-output-string out))))
(define r13 (report 'display
  (let ((out (open-output-string)))
    (display (list "λ" lambda-char 'λ) out)
    (get-output-string out))))
