; SRFI 161 unifiable boxes with the canonical NPL library name.
;
; Boxes form disjoint-set forests: each box points at a parent until it is a
; root, and the root holds the shared value. Path compression runs on every
; lookup. Union always makes the second argument's class the survivor.
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

    (define (link-roots! left right)
      (let ((left-root (find-root left))
            (right-root (find-root right)))
        (if (eq? left-root right-root)
            right-root
            (begin
              (set-ubox-parent! left-root right-root)
              (if (< (ubox-rank left-root) (ubox-rank right-root))
                  0
                  (set-ubox-rank! right-root
                                  (+ (ubox-rank right-root) 1)))
              right-root))))

    (define (ubox-link! left right)
      (let* ((left-root (find-root left))
             (right-root (find-root right))
             (survivor (link-roots! left right)))
        (if (eq? survivor right-root)
            0
            (set-ubox-parent! right-root survivor))
        0))

    (define (ubox-union! left right)
      (link-roots! left right)
      0)

    (define (ubox-unify! proc left right)
      (let ((value (proc (ubox-ref left) (ubox-ref right)))
            (root (link-roots! left right)))
        (set-ubox-content! root value)
        0)))
)
