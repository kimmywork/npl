; SRFI 111 boxes with the canonical NPL library name.
;
; A box holds one or more values. SRFI 111 uses the one-value case; SRFI 195
; uses the general case, and requires both libraries to export the same
; bindings for box, box?, unbox, and set-box!.
(define-library (npl data box)
  (export box box? unbox set-box!)
  (import (scheme base))
  (begin
    (define-record-type box-type
      (make-box values)
      box?
      (values box-values set-box-values!))

    (define (box . values) (make-box values))

    (define (unbox target)
      (apply values (box-values target)))

    (define (set-box! target . values)
      (if (= (length values) (length (box-values target)))
          (set-box-values! target values)
          (error "set-box!: wrong number of values" values)))))
