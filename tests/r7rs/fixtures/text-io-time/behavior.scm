; Textual I/O (R7RS 6.13), with-*-file, features, (scheme time), and
; (scheme complex). Results that vary between runs (clock values) are
; reported as properties.
(define (report name value) (write (list name value)) (newline))
(define path "/tmp/npl-fixture-text-io-time.txt")

(define r1 (report 'read-line
  (let ((in (open-input-string "first\nsecond\n\nlast")))
    (let* ((a (read-line in)) (b (read-line in)) (c (read-line in))
           (d (read-line in)) (e (read-line in)))
      (list a b c d (eof-object? e))))))
(define r2 (report 'read-string
  (let ((in (open-input-string "abcdefg")))
    (let* ((a (read-string 3 in)) (b (read-string 0 in))
           (c (read-string 10 in)) (d (read-string 2 in)))
      (list a b c (eof-object? d))))))
(define r3 (report 'write-string
  (let ((out (open-output-string)))
    (write-string "hello" out)
    (write-string "-world-" out 1)
    (write-string "abcdef" out 2 4)
    (get-output-string out))))
(define r4 (report 'default-ports
  (let ((out (open-output-string)))
    (parameterize ((current-output-port out)
                   (current-input-port (open-input-string "line one\nrest")))
      (write-string (read-line))
      (write-string "|")
      (write-string (read-string 2)))
    (get-output-string out))))
(define r5 (report 'with-output-to-file
  (with-output-to-file path
    (lambda () (display "written") (newline) (write '(1 "two")) 'done))))
(define r6 (report 'with-input-from-file
  (with-input-from-file path
    (lambda ()
      (let* ((line (read-line)) (datum (read)))
        (list line datum))))))
(define r7 (report 'ports-restored
  (list (eq? (current-output-port) (current-output-port))
        (output-port? (current-output-port)))))
(define r8 (report 'char-ready
  (list (char-ready? (open-input-string "x"))
        (char-ready? (open-input-string ""))
        (u8-ready? (open-input-bytevector (bytevector 1))))))
(define r9 (report 'features
  (let ((fs (features)))
    (list (list? fs) (if (memq 'r7rs fs) #t #f)))))
(define r10 (report 'time
  (let* ((j1 (current-jiffy)) (j2 (current-jiffy)) (s (current-second)))
    (list (exact-integer? j1) (<= j1 j2) (exact-integer? (jiffies-per-second))
          (positive? (jiffies-per-second)) (real? s) (inexact? s)
          (> s 1.6e9)))))
; Complex values use inexact parts: Guile 3.0 has no exact complex numbers
; (implementation-allowed; NPL keeps exact parts).
(define r11 (report 'complex
  (let ((z (make-rectangular 3.0 4.0)) (i (make-rectangular 0.0 1.0)))
    (list (magnitude -3) (magnitude z) (magnitude -5/2)
          (angle -1) (angle i) (make-polar 2 0) (make-polar 2.0 0)
          (real-part z) (imag-part z)))))
(define r12 (report 'text-errors
  (list (guard (e ((error-object? e) 'error)) (read-string -1 (open-input-string "")))
        (guard (e ((error-object? e) 'error)) (write-string 1))
        (guard (e ((error-object? e) 'error))
          (write-string "ab" (open-output-string) 0 5)))))
