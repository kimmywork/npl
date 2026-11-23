(define (report name value) (write (list name value)) (newline))

(report 'available (zlib-available?))
(report 'version (> (string-length (zlib-version)) 0))
(report 'empty
        (let ((input #u8())) (equal? (zlib-uncompress (zlib-compress input) 0) input)))
(report 'roundtrip
        (let* ((input (string->utf8 "hello hello hello hello"))
               (compressed (zlib-compress input)))
          (equal? (zlib-uncompress compressed (bytevector-length input))
                  input)))
(report 'bound (> (zlib-compress-bound 100) 100))
