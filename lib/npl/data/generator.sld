; SRFI 158 generators with the canonical NPL library name.
;
; A generator is a thunk returning the next value, or an eof object when it
; is exhausted. Coroutine generators use call/cc; combinators are ordinary
; closures over their source generators.
(define-library (npl data generator)
  (export
    generator circular-generator make-iota-generator make-range-generator
    make-coroutine-generator list->generator vector->generator
    reverse-vector->generator string->generator bytevector->generator
    make-for-each-generator make-unfold-generator

    gcons* gappend gcombine gfilter gremove
    gtake gdrop gtake-while gdrop-while
    gflatten ggroup gmerge gmap gstate-filter
    gdelete gdelete-neighbor-dups gindex gselect

    generator->list generator->reverse-list
    generator->vector generator->vector! generator->string
    generator-fold generator-map->list generator-for-each generator-find
    generator-count generator-any generator-every generator-unfold

    make-accumulator count-accumulator list-accumulator
    reverse-list-accumulator vector-accumulator
    reverse-vector-accumulator vector-accumulator!
    string-accumulator bytevector-accumulator bytevector-accumulator!
    sum-accumulator product-accumulator)
  (import (scheme base) (scheme case-lambda) (npl list))
  (begin
    (define (generator . items)
      (lambda ()
        (if (null? items)
            (eof-object)
            (let ((item (car items)))
              (set! items (cdr items))
              item))))

    (define (circular-generator . items)
      (let ((base items))
        (lambda ()
          (if (null? items) (set! items base))
          (let ((item (car items)))
            (set! items (cdr items))
            item))))

    (define make-iota-generator
      (case-lambda
        ((count) (make-iota-generator count 0 1))
        ((count start) (make-iota-generator count start 1))
        ((count start step)
         (let ((at 0) (value start))
           (lambda ()
             (if (>= at count)
                 (eof-object)
                 (let ((item value))
                   (set! at (+ at 1))
                   (set! value (+ value step))
                   item)))))))

    (define make-range-generator
      (case-lambda
        ((start)
         (let ((value start))
           (lambda ()
             (let ((item value))
               (set! value (+ value 1))
               item))))
        ((start end) (make-range-generator start end 1))
        ((start end step)
         (let ((value start))
           (lambda ()
             (if (if (positive? step) (>= value end) (<= value end))
                 (eof-object)
                 (let ((item value))
                   (set! value (+ value step))
                   item)))))))

    (define (make-coroutine-generator proc)
      (let ((resume #f) (return #f) (done #f))
        (define (yield value)
          (call/cc (lambda (k)
                     (set! resume k)
                     (return value))))
        (lambda ()
          (if done
              (eof-object)
              (call/cc
               (lambda (k)
                 (set! return k)
                 (if resume
                     (begin
                       (resume (if #f #f))
                       (set! done #t)
                       (return (eof-object)))
                     (begin
                       (proc yield)
                       (set! done #t)
                       (return (eof-object))))))))))

    (define (list->generator items)
      (let ((rest items))
        (lambda ()
          (if (null? rest)
              (eof-object)
              (let ((item (car rest)))
                (set! rest (cdr rest))
                item)))))

    (define (vector->generator vector . rest)
      (let ((at (if (pair? rest) (car rest) 0))
            (end (if (and (pair? rest) (pair? (cdr rest)))
                     (cadr rest)
                     (vector-length vector))))
        (lambda ()
          (if (>= at end)
              (eof-object)
              (let ((item (vector-ref vector at)))
                (set! at (+ at 1))
                item)))))

    (define (reverse-vector->generator vector . rest)
      (let ((at (if (pair? rest) (car rest) 0))
            (end (if (and (pair? rest) (pair? (cdr rest)))
                     (cadr rest)
                     (vector-length vector))))
        (lambda ()
          (if (>= at end)
              (eof-object)
              (let ((item (vector-ref vector (- end 1 at))))
                (set! at (+ at 1))
                item)))))

    (define (string->generator string . rest)
      (let ((at (if (pair? rest) (car rest) 0))
            (end (if (and (pair? rest) (pair? (cdr rest)))
                     (cadr rest)
                     (string-length string))))
        (lambda ()
          (if (>= at end)
              (eof-object)
              (let ((item (string-ref string at)))
                (set! at (+ at 1))
                item)))))

    (define (bytevector->generator bytevector . rest)
      (let ((at (if (pair? rest) (car rest) 0))
            (end (if (and (pair? rest) (pair? (cdr rest)))
                     (cadr rest)
                     (bytevector-length bytevector))))
        (lambda ()
          (if (>= at end)
              (eof-object)
              (let ((item (bytevector-u8-ref bytevector at)))
                (set! at (+ at 1))
                item)))))

    (define (make-for-each-generator for-each obj)
      (make-coroutine-generator (lambda (yield) (for-each yield obj))))

    (define (make-unfold-generator stop? mapper successor seed)
      (let ((state seed))
        (lambda ()
          (let loop ()
            (if (stop? state)
                (eof-object)
                (let ((value (mapper state)))
                  (set! state (successor state))
                  value))))))

    (define (gcons* . args)
      (let* ((reversed (reverse args))
             (source (car reversed))
             (items (reverse (cdr reversed))))
        (let ((pending items))
          (lambda ()
            (if (pair? pending)
                (let ((item (car pending)))
                  (set! pending (cdr pending))
                  item)
                (source))))))

    (define (gappend . generators)
      (let ((sources generators))
        (lambda ()
          (let loop ()
            (if (null? sources)
                (eof-object)
                (let ((value ((car sources))))
                  (if (eof-object? value)
                      (begin (set! sources (cdr sources)) (loop))
                      value)))))))

    (define (gcombine proc seed . generators)
      (let ((state seed))
        (lambda ()
          (let ((items (map (lambda (g) (g)) generators)))
            (if (any eof-object? items)
                (eof-object)
                (call-with-values
                 (lambda () (apply proc (append items (list state))))
                 (lambda (value next-state)
                   (set! state next-state)
                   value)))))))

    (define (gfilter pred gen)
      (lambda ()
        (let loop ((value (gen)))
          (cond ((eof-object? value) value)
                ((pred value) value)
                (else (loop (gen)))))))

    (define (gremove pred gen)
      (gfilter (lambda (value) (not (pred value))) gen))

    (define (gtake gen count . rest)
      (let ((padding (if (pair? rest) (car rest) (eof-object))))
        (let ((left count))
          (lambda ()
            (if (<= left 0)
                (eof-object)
                (let ((value (gen)))
                  (set! left (- left 1))
                  (if (eof-object? value) padding value)))))))

    (define (gdrop gen count)
      (let ((left count) (ready #f))
        (lambda ()
          (if (not ready)
              (begin
                (set! ready #t)
                (let loop ((i 0))
                  (if (and (< i left) (not (eof-object? (gen))))
                      (loop (+ i 1))))))
          (gen))))

    (define (gdrop-while pred gen)
      (let ((dropping #t))
        (lambda ()
          (let loop ()
            (let ((value (gen)))
              (cond ((eof-object? value) value)
                    (dropping
                     (if (pred value)
                         (loop)
                         (begin (set! dropping #f) value)))
                    (else value)))))))

    (define (gtake-while pred gen)
      (let ((done #f))
        (lambda ()
          (if done
              (eof-object)
              (let ((value (gen)))
                (if (or (eof-object? value) (pred value))
                    (if (eof-object? value)
                        (begin (set! done #t) value)
                        value)
                    (begin (set! done #t) (eof-object))))))))

    (define (gflatten gen)
      (let ((items '()))
        (lambda ()
          (let loop ()
            (if (null? items)
                (let ((value (gen)))
                  (if (eof-object? value)
                      value
                      (begin (set! items value) (loop))))
                (let ((item (car items)))
                  (set! items (cdr items))
                  item))))))

    (define (ggroup gen count . rest)
      (if (pair? rest)
          (let ((padding (car rest)))
            (let ((inner (ggroup gen count)))
              (lambda ()
                (let ((group (inner)))
                  (if (eof-object? group)
                      group
                      (append group
                              (make-list (- count (length group)) padding)))))))
          (lambda ()
            (let loop ((i 0) (items '()))
              (if (>= i count)
                  (reverse items)
                  (let ((value (gen)))
                    (if (eof-object? value)
                        (if (null? items) value (reverse items))
                        (loop (+ i 1) (cons value items)))))))))

    (define (gmerge2 less? left-gen right-gen)
      (let ((left #f) (right #f) (started #f))
        (lambda ()
          (if (not started)
              (begin
                (set! started #t)
                (set! left (left-gen))
                (set! right (right-gen))))
          (cond ((and (eof-object? left) (eof-object? right)) left)
                ((eof-object? left)
                 (let ((value right))
                   (set! right (right-gen))
                   value))
                ((eof-object? right)
                 (let ((value left))
                   (set! left (left-gen))
                   value))
                ((less? right left)
                 (let ((value right))
                   (set! right (right-gen))
                   value))
                (else
                 (let ((value left))
                   (set! left (left-gen))
                   value))))))

    (define (gmerge less? . generators)
      (if (null? generators)
          (lambda () (eof-object))
          (let loop ((merged (car generators)) (rest (cdr generators)))
            (if (null? rest)
                merged
                (loop (gmerge2 less? merged (car rest)) (cdr rest))))))

    (define (gmap proc . generators)
      (lambda ()
        (let ((items (map (lambda (g) (g)) generators)))
          (if (any eof-object? items)
              (eof-object)
              (apply proc items)))))

    (define (gstate-filter proc seed gen)
      (let ((state seed))
        (lambda ()
          (let loop ()
            (let ((value (gen)))
              (if (eof-object? value)
                  value
                  (call-with-values
                   (lambda () (proc value state))
                   (lambda (keep next-state)
                     (set! state next-state)
                     (if keep value (loop))))))))))

    (define gdelete
      (case-lambda
        ((item gen) (gdelete item gen equal?))
        ((item gen same?)
         (lambda ()
           (let loop ()
             (let ((value (gen)))
               (cond ((eof-object? value) value)
                     ((same? item value) (loop))
                     (else value))))))))

    (define gdelete-neighbor-dups
      (case-lambda
        ((gen) (gdelete-neighbor-dups gen equal?))
        ((gen same?)
         (let ((first #t) (previous #f))
           (lambda ()
             (if first
                 (begin
                   (set! first #f)
                   (set! previous (gen))
                   previous)
                 (let loop ()
                   (let ((value (gen)))
                     (cond ((eof-object? value) value)
                           ((same? previous value) (loop))
                           (else (set! previous value) value))))))))))

    (define (gindex value-gen index-gen)
      (let ((at 0) (done #f))
        (lambda ()
          (if done
              (eof-object)
              (let loop ((value (value-gen)) (index (index-gen)))
                (cond ((or (eof-object? value) (eof-object? index))
                       (set! done #t)
                       (eof-object))
                      ((= index at)
                       (set! at (+ at 1))
                       value)
                      (else
                       (set! at (+ at 1))
                       (loop (value-gen) index))))))))

    (define (gselect value-gen truth-gen)
      (let ((done #f))
        (lambda ()
          (if done
              (eof-object)
              (let loop ((value (value-gen)) (truth (truth-gen)))
                (cond ((or (eof-object? value) (eof-object? truth))
                       (set! done #t)
                       (eof-object))
                      (truth value)
                      (else (loop (value-gen) (truth-gen)))))))))

    (define generator->list
      (case-lambda
        ((gen) (generator->list gen #f))
        ((gen count)
         (let loop ((items '()) (left count))
           (if (and left (<= left 0))
               (reverse items)
               (let ((value (gen)))
                 (if (eof-object? value)
                     (reverse items)
                     (loop (cons value items)
                           (if left (- left 1) #f)))))))))

    (define generator->reverse-list
      (case-lambda
        ((gen) (generator->reverse-list gen #f))
        ((gen count)
         (let loop ((items '()) (left count))
           (if (and left (<= left 0))
               items
               (let ((value (gen)))
                 (if (eof-object? value)
                     items
                     (loop (cons value items)
                           (if left (- left 1) #f)))))))))

    (define generator->vector
      (case-lambda
        ((gen) (list->vector (generator->list gen)))
        ((gen count) (list->vector (generator->list gen count)))))

    (define (generator->vector! vector at gen)
      (let loop ((value (gen)) (count 0) (at at))
        (cond ((eof-object? value) count)
              ((>= at (vector-length vector)) count)
              (else
               (vector-set! vector at value)
               (loop (gen) (+ count 1) (+ at 1))))))

    (define generator->string
      (case-lambda
        ((gen) (list->string (generator->list gen)))
        ((gen count) (list->string (generator->list gen count)))))

    (define (generator-fold proc seed . generators)
      (let loop ((state seed))
        (let ((items (map (lambda (g) (g)) generators)))
          (if (any eof-object? items)
              state
              (loop (apply proc (append items (list state))))))))

    (define (generator-map->list proc . generators)
      (let loop ((items '()))
        (let ((values (map (lambda (g) (g)) generators)))
          (if (any eof-object? values)
              (reverse items)
              (loop (cons (apply proc values) items))))))

    (define (generator-for-each proc . generators)
      (let loop ()
        (let ((values (map (lambda (g) (g)) generators)))
          (if (any eof-object? values)
              (if #f #f)
              (begin
                (apply proc values)
                (loop))))))

    (define (generator-find pred gen)
      (let loop ()
        (let ((value (gen)))
          (cond ((eof-object? value) #f)
                ((pred value) value)
                (else (loop))))))

    (define (generator-count pred gen)
      (generator-fold (lambda (value count) (if (pred value)
                                                (+ count 1)
                                                count))
                      0 gen))

    (define (generator-any pred gen)
      (let loop ()
        (let ((value (gen)))
          (if (eof-object? value)
              #f
              (let ((result (pred value)))
                (if result result (loop)))))))

    (define (generator-every pred gen)
      (let loop ((result #t))
        (let ((value (gen)))
          (if (eof-object? value)
              result
              (let ((next (pred value)))
                (if next (loop next) #f))))))

    (define (generator-unfold gen unfold . rest)
      (apply unfold eof-object? (lambda (x) x) (lambda (x) (gen)) (gen) rest))

    (define (make-accumulator kons knil finalize)
      (let ((state knil))
        (lambda (obj)
          (if (eof-object? obj)
              (finalize state)
              (set! state (kons obj state))))))

    (define (count-accumulator)
      (make-accumulator (lambda (obj state) (+ state 1)) 0 (lambda (x) x)))
    (define (list-accumulator)
      (make-accumulator cons '() reverse))
    (define (reverse-list-accumulator)
      (make-accumulator cons '() (lambda (x) x)))
    (define (vector-accumulator)
      (make-accumulator cons '() (lambda (x) (list->vector (reverse x)))))
    (define (reverse-vector-accumulator)
      (make-accumulator cons '() list->vector))
    (define (vector-accumulator! vector at)
      (lambda (obj)
        (if (eof-object? obj)
            vector
            (begin
              (vector-set! vector at obj)
              (set! at (+ at 1))))))
    (define (string-accumulator)
      (make-accumulator cons '()
                        (lambda (x) (list->string (reverse x)))))
    (define (list->bytevector items)
      (let ((result (make-bytevector (length items) 0)))
        (let loop ((items items) (at 0))
          (if (pair? items)
              (begin
                (bytevector-u8-set! result at (car items))
                (loop (cdr items) (+ at 1)))))
        result))
    (define (bytevector-accumulator)
      (make-accumulator cons '()
                        (lambda (x) (list->bytevector (reverse x)))))
    (define (bytevector-accumulator! bytevector at)
      (lambda (obj)
        (if (eof-object? obj)
            bytevector
            (begin
              (bytevector-u8-set! bytevector at obj)
              (set! at (+ at 1))))))
    (define (sum-accumulator) (make-accumulator + 0 (lambda (x) x)))
    (define (product-accumulator) (make-accumulator * 1 (lambda (x) x)))))
