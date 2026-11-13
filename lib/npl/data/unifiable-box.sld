; SRFI 161 unifiable boxes with the canonical NPL library name.
;
; Boxes form disjoint-set forests: each box points at a parent until it is a
; root, and the root holds the shared value. Path compression runs on every
; lookup. Every union uses rank; ubox-link! additionally keeps the second
; argument's value in the surviving class.
(define-library (npl data unifiable-box)
  (export ubox ubox? ubox-ref ubox-set! ubox-unify! ubox-union! ubox-link! ubox=?)
  (import (scheme base))
  (begin
    (define-record-type ubox-type
      (make-ubox parent rank content)
      ubox?
      (parent ubox-parent set-ubox-parent!)
      (rank ubox-rank set-ubox-rank!)
      (content ubox-content set-ubox-content!))

    (define (ubox value) (make-ubox #f 0 value))

    (define (find-root target)
      (let ((parent (ubox-parent target)))
        (if (not parent)
            target
            (let ((root (find-root parent)))
              (set-ubox-parent! target root)
              root))))

    (define (ubox-ref target)
      (ubox-content (find-root target)))

    (define (ubox-set! target value)
      (set-ubox-content! (find-root target) value))

    (define (ubox=? left right)
      (eq? (find-root left) (find-root right)))

    (define (union-roots! left right)
      (let ((left-root (find-root left))
            (right-root (find-root right)))
        (cond ((eq? left-root right-root) left-root)
              ((< (ubox-rank left-root) (ubox-rank right-root))
               (set-ubox-parent! left-root right-root)
               right-root)
              ((> (ubox-rank left-root) (ubox-rank right-root))
               (set-ubox-parent! right-root left-root)
               left-root)
              (else
               (set-ubox-parent! right-root left-root)
               (set-ubox-rank! left-root (+ (ubox-rank left-root) 1))
               left-root))))

    (define (ubox-link! left right)
      (let ((right-value (ubox-ref right)))
        (if (ubox=? left right)
            0
            (let ((survivor (union-roots! left right)))
              (set-ubox-content! survivor right-value)
              0))))

    (define (ubox-union! left right)
      (union-roots! left right)
      0)

    (define (ubox-unify! proc left right)
      (let ((value (proc (ubox-ref left) (ubox-ref right)))
            (root (union-roots! left right)))
        (set-ubox-content! root value)
        0)))
)
