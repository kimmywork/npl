; R13: ffi_alloc_block stores a freshly allocated block into the pointer box
; after a further allocation, without a write barrier.  A minor collection
; between the two promotes the box; the young block is then swept while the
; owner still points at it.
; Arguments: N owners (default 300000), F filler vectors per owner (default 8).
; Expected: "bad owners: 0".  run.sh --gc runs N=20000 F=1 under small
; nurseries and NPL_GC_VERIFY.
(import (scheme base) (scheme write) (scheme process-context) (npl ffi))
(define (argument index default)
  (let loop ((args (cdr (command-line))) (index index))
    (cond ((null? args) default)
          ((= index 0) (string->number (car args)))
          (else (loop (cdr args) (- index 1))))))
(define n (argument 0 300000))
(define fill (argument 1 8))
(define keep (make-vector n #f))
(let loop ((i 0))
  (if (< i n)
      (begin
        (vector-set! keep i (foreign-alloc 8))
        (let filler ((k 0))
          (if (< k fill) (begin (make-vector 4 k) (filler (+ k 1)))))
        (loop (+ i 1)))))
(define first-message #f)
(let loop ((i 0) (bad 0))
  (if (< i n)
      (loop (+ i 1)
            (+ bad (guard (e (#t (if (not first-message)
                                     (set! first-message
                                           (error-object-message e)))
                                 1))
                     (pointer-set-u8! (vector-ref keep i) 1)
                     0)))
      (begin
        (display "bad owners: ") (display bad)
        (if first-message
            (begin (display " (") (display first-message) (display ")")))
        (newline))))
