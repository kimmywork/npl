; Shared by (scheme base) char comparisons and the evaluator's spread
; comparisons: COMPARE holds for every adjacent pair of VALUES (at least one).
; Included rather than imported, so neither library gains a dependency.
(define (ordered-chain? compare values)
  (if (null? (cdr values))
      #t
      (if (compare (car values) (car (cdr values)))
          (ordered-chain? compare (cdr values))
          #f)))
