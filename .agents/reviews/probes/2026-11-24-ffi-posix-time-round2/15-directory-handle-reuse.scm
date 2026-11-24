; R15: directory handles are looked up by malloc address in a global list.
; After close-directory frees the entry, a later open-directory can reuse the
; address, and the stale handle then reads and closes the new directory.
; Expected: "no reuse" or every stale operation raising "directory is closed".
(import (scheme base) (scheme write) (npl unix))
(define (attempt thunk)
  (guard (e (#t (list 'error (error-object-message e)))) (thunk)))
(let loop ((i 0))
  (if (< i 20)
      (let ((a (open-directory "/usr")))
        (close-directory a)
        (let* ((b (open-directory "/etc"))
               (stale (attempt (lambda () (read-directory a)))))
          (if (string? stale)
              (begin
                (write (list 'stale-read 'from-etc))
                (display " ")
                (write (list 'stale-close
                             (attempt (lambda () (close-directory a) 'ok))))
                (display " ")
                (write (list 'b-after
                             (attempt (lambda () (read-directory b)))))
                (newline))
              (begin (close-directory b) (loop (+ i 1))))))
      (begin (write 'no-reuse) (newline))))
