; Current ports are parameter objects (R7RS 6.13.1) and binary port
; procedures (6.13.2-3). Argument order inside list is unspecified, so each
; side-effecting read is bound with let before it is reported.
(define (report name value) (write (list name value)) (newline))
(define (bytes->list bv)
  (let loop ((i (- (bytevector-length bv) 1)) (acc '()))
    (if (< i 0) acc (loop (- i 1) (cons (bytevector-u8-ref bv i) acc)))))

(define r1 (report 'parameters
  (list (procedure? current-input-port) (procedure? current-output-port)
        (procedure? current-error-port)
        (input-port? (current-input-port)) (output-port? (current-output-port))
        (output-port? (current-error-port)))))
(define r2 (report 'captured-output
  (let ((out (open-output-string)))
    (parameterize ((current-output-port out))
      (display "text ")
      (write "quoted")
      (write-char #\!)
      (newline))
    (get-output-string out))))
(define r3 (report 'redirected-input
  (let ((in (open-input-string "ab (1 2)")))
    (parameterize ((current-input-port in))
      (let* ((first (read-char))
             (second (peek-char))
             (third (read-char))
             (datum (read)))
        (list first second third datum))))))
(define r4 (report 'error-port
  (let ((err (open-output-string)))
    (parameterize ((current-error-port err))
      (display "warning" (current-error-port)))
    (get-output-string err))))
(define r5 (report 'restored-after-escape
  (let ((outer (current-output-port)))
    (call-with-current-continuation
      (lambda (k)
        (parameterize ((current-output-port (open-output-string)))
          (k #f))))
    (eq? outer (current-output-port)))))
(define r6 (report 'nested
  (let ((a (open-output-string)) (b (open-output-string)))
    (parameterize ((current-output-port a))
      (display 1)
      (parameterize ((current-output-port b)) (display 2))
      (display 3))
    (list (get-output-string a) (get-output-string b)))))
(define r7 (report 'binary-output
  (let ((out (open-output-bytevector)))
    (write-u8 65 out)
    (write-bytevector (bytevector 66 67 68) out 1)
    (bytes->list (get-output-bytevector out)))))
(define r8 (report 'binary-input
  (let ((in (open-input-bytevector (bytevector 1 2 3 4))))
    (let* ((peeked (peek-u8 in))
           (first (read-u8 in))
           (pair (bytes->list (read-bytevector 2 in)))
           (buffer (make-bytevector 3 0))
           (count (read-bytevector! buffer in 1))
           (end (eof-object? (read-u8 in))))
      (list peeked first pair count (bytes->list buffer) end)))))
(define r9 (report 'port-kinds
  (list (textual-port? (open-input-string "")) (binary-port? (open-input-bytevector (bytevector)))
        (port? 1) (eof-object? (eof-object)))))
