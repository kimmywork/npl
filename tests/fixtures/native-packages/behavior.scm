(define (report name value) (write (list name value)) (newline))

(report 'empty (crc32 #u8()))
(report 'digits (crc32 (string->utf8 "123456789")))
(report 'zero (crc32 #u8(0)))
