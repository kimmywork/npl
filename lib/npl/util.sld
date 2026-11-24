; Small helpers with no equivalent in the other NPL libraries.  String
; predicates and searches come from (npl string) (SRFI 13); add a helper here
; only when no existing library already provides the behavior, and give it a
; name that no SRFI uses for something else.
(define-library (npl util)
  (export path-join path-directory path-normalize exact-natural?)
  (import (scheme base))
  (begin
    ; LEFT/RIGHT with exactly one "/" between them; an empty LEFT or one
    ; ending in "/" is not doubled.
    (define (path-join left right)
      (if (if (= (string-length left) 0)
              #t
              (char=? (string-ref left (- (string-length left) 1)) #\/))
          (string-append left right)
          (string-append left "/" right)))

    ; Everything up to and including the last "/", or "" without one.
    (define (path-directory path)
      (let loop ((index (- (string-length path) 1)))
        (cond
          ((< index 0) "")
          ((char=? (string-ref path index) #\/)
           (substring path 0 (+ index 1)))
          (else (loop (- index 1))))))

    ; Remove "." segments and fold "dir/.." pairs, so two spellings of one
    ; file compare equal: "lib/scheme/internal/../x.scm" -> "lib/scheme/x.scm".
    ; A leading ".." that has nothing to fold is kept.
    (define (path-normalize path)
      (let* ((absolute (if (> (string-length path) 0)
                           (char=? (string-ref path 0) #\/)
                           #f))
             (segments
               (let loop ((start 0) (index 0) (out '()))
                 (cond
                   ((= index (string-length path))
                    (reverse (cons (substring path start index) out)))
                   ((char=? (string-ref path index) #\/)
                    (loop (+ index 1) (+ index 1)
                          (cons (substring path start index) out)))
                   (else (loop start (+ index 1) out)))))
             (kept
               (let loop ((rest segments) (stack '()))
                 (cond
                   ((null? rest) (reverse stack))
                   ((member (car rest) '("" "."))
                    (loop (cdr rest) stack))
                   ((if (string=? (car rest) "..")
                        (if (pair? stack)
                            (not (string=? (car stack) ".."))
                            #f)
                        #f)
                    (loop (cdr rest) (cdr stack)))
                   (else (loop (cdr rest) (cons (car rest) stack)))))))
        (let loop ((rest kept) (out (if absolute "/" "")))
          (cond
            ((null? rest) (if (string=? out "") "." out))
            ((if (string=? out "") #t (string=? out "/"))
             (loop (cdr rest) (string-append out (car rest))))
            (else (loop (cdr rest) (string-append out "/" (car rest))))))))

    (define (exact-natural? value)
      (if (exact-integer? value) (>= value 0) #f))))
