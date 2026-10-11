; (scheme base) textual I/O built on the port primitives (R7RS 6.13.2-3),
; plus char-ready?/u8-ready? and features. Optional ports default to the
; current-port parameters.

(define (input-port-argument optional)
  (if (pair? optional) (car optional) (current-input-port)))

(define (output-port-argument optional)
  (if (pair? optional) (car optional) (current-output-port)))

(define (read-line . port)
  (let ((in (input-port-argument port)))
    (let loop ((characters '()))
      (let ((c (read-char in)))
        (cond
          ((eof-object? c)
           (if (null? characters) c (list->string (reverse characters))))
          ((char=? c #\newline) (list->string (reverse characters)))
          (else (loop (cons c characters))))))))

(define (read-string k . port)
  (if (not (if (exact-integer? k) (raw-less-equal 0 k) #f))
      (argument-error "read-string" "expected a non-negative exact count" k))
  (let ((in (input-port-argument port)))
    (let loop ((remaining k) (characters '()))
      (if (raw-equal remaining 0)
          (list->string (reverse characters))
          (let ((c (read-char in)))
            (if (eof-object? c)
                (if (null? characters) c (list->string (reverse characters)))
                (loop (raw-subtract remaining 1) (cons c characters))))))))

; (write-string string [port [start [end]]])
(define (write-string string . optional)
  (if (not (string? string))
      (argument-error "write-string" "expected a string" string))
  (let ((out (output-port-argument optional))
        (bounds (checked-range "write-string" (string-length string)
                               (if (pair? optional) (cdr optional) '()))))
    (let loop ((index (car bounds)))
      (if (raw-less index (cdr bounds))
          (begin
            (write-char (string-ref string index) out)
            (loop (raw-add index 1)))))))

(define (char-ready? . port) (npl_port_ready_p (input-port-argument port)))
(define (u8-ready? . port) (npl_byte_ready_p (input-port-argument port)))

; The feature identifiers cond-expand accepts (frontend/library.npl).
(define (features) (list 'r7rs 'npl))
