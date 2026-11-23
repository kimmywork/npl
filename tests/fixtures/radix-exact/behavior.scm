(define (report name value) (write (list name value)) (newline))
(define (read-number text)
  (let ((port (open-input-string text)))
    (let ((value (read port))) (close-input-port port) value)))
(define (radix-results)
  (report 'literals
    (list #b101010 #o52 #d42 #x2a #x-2a #xdeadbeef
          '#(#b11 #o10 #d9 #x10) '(#x2a . #b101)))
  (report 'read
    (list (read-number "#b101010") (read-number "#o52")
          (read-number "#d42") (read-number "#x2A")
          (read-number "#x-2a") (read-number "#xdeadbeef")))
  (report 'conversion
    (list (number->string 42 2) (number->string 42 8)
          (number->string 42 10) (number->string 42 16)
          (number->string -42 16) (number->string 1/2 16)
          (string->number "101010" 2) (string->number "52" 8)
          (string->number "2a" 16) (string->number "-2A" 16)
          (string->number "a/b" 16) (string->number "2" 2)
          (string->number "z" 36)))
  (report 'roundtrip
    (list (= #x123456789abcdef
             (string->number (number->string #x123456789abcdef 16) 16))
          (= #x-123456789abcdef
             (string->number (number->string #x-123456789abcdef 16) 16))
          (= 17/31 (string->number (number->string 17/31 16) 16)))))
(define fixture-result (begin (radix-results) 0))
