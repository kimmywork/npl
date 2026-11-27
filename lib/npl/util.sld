; Small helpers with no equivalent in the other NPL libraries.  String
; predicates and searches come from (npl string) (SRFI 13); add a helper here
; only when no existing library already provides the behavior, and give it a
; name that no SRFI uses for something else.
(define-library (npl util)
  (export path-separator? path-join path-directory path-normalize exact-natural?)
  (import (scheme base))
  (begin
    ; Directory separator predicate: "/" everywhere, plus "\" on Windows.
    (define (path-separator? ch)
      (cond-expand
        (windows (or (char=? ch #\/) (char=? ch #\\)))
        (else (char=? ch #\/))))

    ; LEFT/RIGHT with exactly one separator between them; an empty LEFT or one
    ; ending in a separator is not doubled.
    (define (path-join left right)
      (if (if (= (string-length left) 0)
              #t
              (path-separator? (string-ref left (- (string-length left) 1))))
          (string-append left right)
          (string-append left "/" right)))

    ; Everything up to and including the last separator, or "" without one.
    (define (path-directory path)
      (let loop ((index (- (string-length path) 1)))
        (cond
          ((< index 0) "")
          ((path-separator? (string-ref path index))
           (substring path 0 (+ index 1)))
          (else (loop (- index 1))))))

    ; Remove "." segments and fold "dir/.." pairs, so two spellings of one
    ; file compare equal: "lib/scheme/internal/../x.scm" -> "lib/scheme/x.scm".
    ; A leading ".." that has nothing to fold is kept.  Preserves root prefixes
    ; ("/" on POSIX; plus UNC "//", drive root "C:/", and drive-relative "C:"
    ; on Windows).
    (define (path-normalize path)
      (let* ((len (string-length path))
             (root
               (cond-expand
                 (windows
                   (cond
                     ((and (>= len 2)
                           (path-separator? (string-ref path 0))
                           (path-separator? (string-ref path 1)))
                      (cons "//" 2))
                     ((and (>= len 3)
                           (char=? (string-ref path 1) #\:)
                           (path-separator? (string-ref path 2)))
                      (cons (string-append (substring path 0 2) "/") 3))
                     ((and (>= len 2)
                           (char=? (string-ref path 1) #\:))
                      (cons (substring path 0 2) 2))
                     ((and (> len 0)
                           (path-separator? (string-ref path 0)))
                      (cons "/" 1))
                     (else (cons "" 0))))
                 (else
                   (if (and (> len 0) (path-separator? (string-ref path 0)))
                       (cons "/" 1)
                       (cons "" 0)))))
             (prefix (car root))
             (body-start (cdr root))
             (segments
               (let loop ((start body-start) (index body-start) (out '()))
                 (cond
                   ((= index len)
                    (reverse (cons (substring path start index) out)))
                   ((path-separator? (string-ref path index))
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
        (if (null? kept)
            (cond
              ((string=? prefix "") ".")
              ((char=? (string-ref prefix (- (string-length prefix) 1)) #\/)
               prefix)
              (else (string-append prefix ".")))
            (let loop ((rest (cdr kept))
                       (out (string-append prefix (car kept))))
              (if (null? rest)
                  out
                  (loop (cdr rest)
                        (string-append out "/" (car rest))))))))

    (define (exact-natural? value)
      (if (exact-integer? value) (>= value 0) #f))))
