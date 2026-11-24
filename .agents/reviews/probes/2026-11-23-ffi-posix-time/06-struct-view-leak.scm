; R5: make-c-struct allocates with foreign-alloc and nothing frees it;
; (npl time) does so on every current-time.  Modes (second argument):
;   time    call current-time N times (the real leak)
;   leak    N x (foreign-alloc 16), never freed
;   free    N x (foreign-alloc 16) + foreign-free (control: GC heap only)
; run.sh --leak compares peak RSS; leak - free is the malloc growth.
(import (scheme base) (scheme process-context) (npl ffi) (npl time))
(define n (string->number (cadr (command-line))))
(define mode (string->symbol (car (cddr (command-line)))))
(let loop ((i 0))
  (if (< i n)
      (begin
        (cond ((eq? mode 'time) (current-time))
              ((eq? mode 'leak) (foreign-alloc 16))
              (else (foreign-free (foreign-alloc 16))))
        (loop (+ i 1)))))
