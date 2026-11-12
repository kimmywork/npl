; SRFI 195 multiple-value boxes with the canonical NPL library name.
;
; box, box?, unbox, and set-box! are re-exported from (npl data box), so
; (srfi 111) and (srfi 195) share the same bindings, as SRFI 195 requires.
(define-library (npl data multiple-value-box)
  (export box box? unbox set-box! box-arity unbox-value set-box-value!)
  (import (scheme base) (npl data box))
  (begin
    (define (%replace-at items index value)
      (if (= index 0)
          (cons value (cdr items))
          (cons (car items)
                (%replace-at (cdr items) (- index 1) value))))

    (define (box-arity target)
      (length (box-values-of target)))

    (define (box-values-of target)
      (call-with-values (lambda () (unbox target)) list))

    (define (unbox-value target index)
      (let ((items (box-values-of target)))
        (if (and (exact-integer? index) (<= 0 index (- (length items) 1)))
            (list-ref items index)
            (error "unbox-value: invalid index" index))))

    (define (set-box-value! target index value)
      (let ((items (box-values-of target)))
        (if (and (exact-integer? index) (<= 0 index (- (length items) 1)))
            (begin
              (apply set-box! target (%replace-at items index value)))
            (error "set-box-value!: invalid index" index))))))
