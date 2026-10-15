; String building through a string port and string operations.
(define (bench-main)
  (let ((out (open-output-string)))
    (let loop ((i 0))
      (if (< i 200000)
          (begin (write-string (number->string i) out) (write-char #\space out) (loop (+ i 1)))))
    (let ((s (get-output-string out)))
      (display (string-length s)) (newline)
      (display (string-length (string-upcase (substring s 0 100000)))) (newline))))
