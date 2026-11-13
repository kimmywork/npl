; SRFI 44 collections for lists, vectors, and strings, with the canonical NPL
; library name.
;
; The reference SRFI 44 specifies a generic dispatch layer over many
; collection types. This library implements the part the 0.1.2 plan calls for:
; lists, vectors, and strings are collections, ordered collections, limited
; collections, and sequences; lists are additionally flexible and directional.
; Bags, sets, maps, and dictionaries are not part of this subset.
;
; The procedures named like their (scheme base) counterparts are re-exported
; with their standard identity, except list-ref, vector-ref, string-ref, and
; list-copy, whose SRFI 44 forms accept optional range or absence arguments.
(define-library (npl data collection)
  (export
    collection? collection-name
    collection-size collection-empty? collection-count
    collection-get-any collection->list collection-copy
    collection-clear collection-clear!
    collection-fold-left collection-fold-right
    collection-fold-keys-left collection-fold-keys-right
    ordered-collection? limited-collection? directional-collection?
    purely-mutable-collection? sequence? flexible-sequence?
    bag? set? map? dictionary?

    list list? make-list list-copy list->list list-size list-empty?
    list-contains? list-ref list-get-any list-get-left list-get-right
    list-count list= list-fold-left list-fold-right list-equivalence-function
    list-add list-add! list-set list-set! list-insert-left list-insert-left!
    list-insert-right list-insert-right! list-delete list-delete!
    list-delete-left list-delete-left! list-delete-right list-delete-right!
    list-delete-all list-delete-all! list-clear list-clear!
    list-insert list-insert! list-delete-at list-delete-at!
    list-replace-from list-replace-from!

    vector vector? make-vector vector-copy vector->list vector-size
    vector-empty? vector-contains? vector-count vector-ref vector-get-any
    vector-get-left vector-get-right vector-set vector-set!
    vector-replace-from vector-replace-from! vector= vector-fold-left
    vector-fold-right vector-equivalence-function

    string string? make-string string-copy string->list string-size
    string-empty? string-contains? string-count string-ref string-get-any
    string-get-left string-get-right string-set string-set!
    string-replace-from string-replace-from! string=
    string-fold-left string-fold-right string-equivalence-function

    sequence-ref sequence-size sequence-get-left sequence-get-right)
  (import (scheme base))
  (begin
    (define (collection-kind value)
      (cond ((list? value) 'list)
            ((vector? value) 'vector)
            ((string? value) 'string)
            (else #f)))

    (define (collection? value) (if (collection-kind value) #t #f))

    (define (collection-name value)
      (let ((kind (collection-kind value)))
        (if kind kind (error "collection-name" "not a collection" value))))

    (define (ordered-collection? value) (collection? value))
    (define (limited-collection? value) (collection? value))
    (define (directional-collection? value) (eq? (collection-kind value) 'list))
    (define (purely-mutable-collection? value) #f)
    (define (sequence? value) (collection? value))
    (define (flexible-sequence? value) (eq? (collection-kind value) 'list))
    (define (bag? value) #f)
    (define (set? value) #f)
    (define (map? value) #f)
    (define (dictionary? value) #f)

    (define (collection->list value)
      (cond ((list? value) value)
            ((vector? value) (vector->list value))
            ((string? value) (string->list value))
            (else (error "collection->list" "not a collection" value))))

    (define (collection-size value)
      (cond ((list? value) (length value))
            ((vector? value) (vector-length value))
            ((string? value) (string-length value))
            (else (error "collection-size" "not a collection" value))))

    (define (collection-empty? value) (zero? (collection-size value)))

    (define (collection-equivalence value)
      (if (eq? (collection-kind value) 'string) char=? equal?))

    (define (collection-count value element)
      (let ((same? (collection-equivalence value)))
        (let loop ((items (collection->list value)) (total 0))
          (if (null? items)
              total
              (loop (cdr items)
                    (if (same? (car items) element) (+ total 1) total))))))

    (define (absence-value rest)
      (if (pair? rest) ((car rest)) #f))

    (define (collection-get-any value . rest)
      (let ((items (collection->list value)))
        (if (null? items) (absence-value rest) (car items))))

    (define (collection-copy value)
      (cond ((list? value) (list-copy value))
            ((vector? value) (vector-copy value))
            ((string? value) (string-copy value))
            (else (error "collection-copy" "not a collection" value))))

    (define (collection-clear value)
      (cond ((list? value) '())
            ((vector? value) (make-vector 0))
            ((string? value) "")
            (else (error "collection-clear" "not a collection" value))))

    (define (collection-clear! value)
      (if (eq? (collection-kind value) 'vector)
          (vector-fill! value 0)
          (error "collection-clear!" "only vectors can be cleared in place"))
      (if #f #f))

    ; SRFI 44 folds pass the collection value first and the seeds after it.
    ; The fold function returns a proceed flag followed by the next seeds.
    (define (fold-left-values items function seeds)
      (if (null? items)
          (apply values seeds)
          (call-with-values
            (lambda () (apply function (car items) seeds))
            (lambda (proceed . next)
              (if proceed
                  (fold-left-values (cdr items) function next)
                  (apply values next))))))

    (define (fold-right-values items function seeds)
      (fold-left-values (reverse items) function seeds))

    (define (collection-fold-left value function . seeds)
      (fold-left-values (collection->list value) function seeds))

    (define (collection-fold-right value function . seeds)
      (fold-right-values (collection->list value) function seeds))

    (define (collection-fold-keys-left value function . seeds)
      (apply collection-fold-left value function seeds))

    (define (collection-fold-keys-right value function . seeds)
      (apply collection-fold-right value function seeds))

;;;; Lists

    (define (list->list value) value)

    (define (list-size value) (length value))
    (define (list-empty? value) (null? value))
    (define (list-equivalence-function value) equal?)
    (define (list-contains? value element) (if (member element value) #t #f))

    (define (nth-item items index)
      (if (null? items)
          #f
          (if (zero? index) (car items) (nth-item (cdr items) (- index 1)))))

    (define (list-get-any value . rest) (apply collection-get-any value rest))
    (define (list-get-left value . rest)
      (if (null? value) (absence-value rest) (car value)))
    (define (list-get-right value . rest)
      (if (null? value)
          (absence-value rest)
          (car (list-tail value (- (length value) 1)))))

    (define (list-count value element) (collection-count value element))

    (define (list= same? . lists)
      (let loop ((rest lists))
        (cond ((null? rest) #t)
              ((null? (cdr rest)) #t)
              ((= (length (car rest)) (length (car (cdr rest))))
               (if (let pair-loop ((left (car rest)) (right (car (cdr rest))))
                     (cond ((null? left) #t)
                           ((same? (car left) (car right))
                            (pair-loop (cdr left) (cdr right)))
                           (else #f)))
                   (loop (cdr rest))
                   #f))
              (else #f))))

    (define (list-fold-left value function . seeds)
      (fold-left-values value function seeds))

    (define (list-fold-right value function . seeds)
      (fold-right-values value function seeds))

    (define (list-add value element) (append value (list element)))
    (define (list-add! value element) (append value (list element)))

    (define (copy-list-with-index value index replacement)
      (let loop ((items value) (position 0))
        (if (null? items)
            '()
            (cons (if (= position index) replacement (car items))
                  (loop (cdr items) (+ position 1))))))

    (define (list-set value index replacement)
      (if (if (exact-integer? index)
              (if (<= 0 index) (< index (length value)) #f)
              #f)
          (copy-list-with-index value index replacement)
          (error "list-set" "index out of range" index)))

    (define (list-insert-left value element) (cons element value))
    (define (list-insert-left! value element) (cons element value))
    (define (list-insert-right value element) (append value (list element)))
    (define (list-insert-right! value element) (append value (list element)))

    (define (delete-first same? items element)
      (cond ((null? items) '())
            ((same? (car items) element) (cdr items))
            (else (cons (car items)
                        (delete-first same? (cdr items) element)))))

    (define (delete-all same? items element)
      (cond ((null? items) '())
            ((same? (car items) element)
             (delete-all same? (cdr items) element))
            (else (cons (car items)
                        (delete-all same? (cdr items) element)))))

    (define (list-delete value element) (delete-first equal? value element))
    (define (list-delete! value element) (delete-first equal? value element))
    (define (list-delete-all value element) (delete-all equal? value element))
    (define (list-delete-all! value element) (delete-all equal? value element))

    (define (list-delete-left value) (if (null? value) '() (cdr value)))
    (define (list-delete-left! value) (if (null? value) '() (cdr value)))
    (define (list-delete-right value)
      (if (null? value)
          '()
          (reverse (cdr (reverse value)))))
    (define (list-delete-right! value)
      (if (null? value)
          '()
          (reverse (cdr (reverse value)))))

    (define (list-clear value) '())
    (define (list-clear! value) '())

    (define (list-insert value index element)
      (if (if (exact-integer? index)
              (if (<= 0 index) (<= index (length value)) #f)
              #f)
          (let loop ((items value) (position 0))
            (if (= position index)
                (cons element items)
                (cons (car items)
                      (loop (cdr items) (+ position 1)))))
          (error "list-insert" "index out of range" index)))

    (define (list-insert! value index element) (list-insert value index element))

    (define (list-delete-at value index)
      (if (if (exact-integer? index)
              (if (<= 0 index) (< index (length value)) #f)
              #f)
          (let loop ((items value) (position 0))
            (cond ((null? items) '())
                  ((= position index) (cdr items))
                  (else (cons (car items)
                              (loop (cdr items) (+ position 1))))))
          (error "list-delete-at" "index out of range" index)))

    (define (list-delete-at! value index) (list-delete-at value index))

    (define (list-replace-from value start source . rest)
      (let* ((source-start (if (pair? rest) (car rest) 0))
             (source-end (if (if (pair? rest) (pair? (cdr rest)) #f)
                             (car (cdr rest))
                             (length source)))
             (replacement (list-range-copy source source-start source-end)))
        (append (list-range-copy value 0 start)
                replacement
                (list-range-copy value (+ start (length replacement))
                                 (length value)))))

    (define (list-replace-from! value start source . rest)
      (apply list-replace-from value start source rest))

    (define (list-range-copy value start end)
      (let loop ((items (list-tail value start))
                 (remaining (- end start))
                 (result '()))
        (if (or (null? items) (<= remaining 0))
            (reverse result)
            (loop (cdr items) (- remaining 1) (cons (car items) result)))))

;;;; Vectors

    (define (vector-size value) (vector-length value))
    (define (vector-empty? value) (zero? (vector-length value)))
    (define (vector-equivalence-function value) equal?)

    (define (vector-contains? value element)
      (if (member element (vector->list value)) #t #f))
    (define (vector-count value element) (collection-count value element))
    (define (vector-get-any value . rest) (apply collection-get-any value rest))
    (define (vector-get-left value . rest)
      (if (zero? (vector-length value)) (absence-value rest) (vector-ref value 0)))
    (define (vector-get-right value . rest)
      (if (zero? (vector-length value))
          (absence-value rest)
          (vector-ref value (- (vector-length value) 1))))

    (define (vector-fold-left value function . seeds)
      (fold-left-values (vector->list value) function seeds))
    (define (vector-fold-right value function . seeds)
      (fold-right-values (vector->list value) function seeds))

    (define (vector= same? . vectors)
      (let loop ((rest vectors))
        (cond ((null? rest) #t)
              ((null? (cdr rest)) #t)
              ((= (vector-length (car rest)) (vector-length (car (cdr rest))))
               (if (let pair-loop ((index 0))
                     (if (>= index (vector-length (car rest)))
                         #t
                         (if (same? (vector-ref (car rest) index)
                                    (vector-ref (car (cdr rest)) index))
                             (pair-loop (+ index 1))
                             #f)))
                   (loop (cdr rest))
                   #f))
              (else #f))))

    (define (vector-set value index replacement)
      (let ((result (vector-copy value)))
        (vector-set! result index replacement)
        result))

    (define (vector-replace-from value start source . rest)
      (let* ((source-start (if (pair? rest) (car rest) 0))
             (source-end (if (if (pair? rest) (pair? (cdr rest)) #f)
                             (car (cdr rest))
                             (vector-length source)))
             (count (- source-end source-start))
             (result (vector-copy value)))
        (let loop ((offset 0))
          (if (< offset count)
              (begin
                (vector-set! result (+ start offset)
                             (vector-ref source (+ source-start offset)))
                (loop (+ offset 1)))))
        result))

    (define (vector-replace-from! value start source . rest)
      (let* ((source-start (if (pair? rest) (car rest) 0))
             (source-end (if (if (pair? rest) (pair? (cdr rest)) #f)
                             (car (cdr rest))
                             (vector-length source))))
        (let loop ((offset 0))
          (if (< offset (- source-end source-start))
              (begin
                (vector-set! value (+ start offset)
                             (vector-ref source (+ source-start offset)))
                (loop (+ offset 1)))))))

;;;; Strings

    (define (string-size value) (string-length value))
    (define (string-empty? value) (zero? (string-length value)))
    (define (string-equivalence-function value) char=?)

    (define (string-contains? value character)
      (if (memv character (string->list value)) #t #f))
    (define (string-count value character) (collection-count value character))
    (define (string-get-any value . rest) (apply collection-get-any value rest))
    (define (string-get-left value . rest)
      (if (zero? (string-length value)) (absence-value rest) (string-ref value 0)))
    (define (string-get-right value . rest)
      (if (zero? (string-length value))
          (absence-value rest)
          (string-ref value (- (string-length value) 1))))

    (define (string-fold-left value function . seeds)
      (fold-left-values (string->list value) function seeds))
    (define (string-fold-right value function . seeds)
      (fold-right-values (string->list value) function seeds))

    (define (string= same? . strings)
      (let loop ((rest strings))
        (cond ((null? rest) #t)
              ((null? (cdr rest)) #t)
              ((= (string-length (car rest)) (string-length (car (cdr rest))))
               (if (let char-loop ((index 0))
                     (if (>= index (string-length (car rest)))
                         #t
                         (if (same? (string-ref (car rest) index)
                                    (string-ref (car (cdr rest)) index))
                             (char-loop (+ index 1))
                             #f)))
                   (loop (cdr rest))
                   #f))
              (else #f))))

    (define (string-set value index replacement)
      (let ((result (string-copy value)))
        (string-set! result index replacement)
        result))

    (define (string-replace-from value start source . rest)
      (let* ((source-start (if (pair? rest) (car rest) 0))
             (source-end (if (if (pair? rest) (pair? (cdr rest)) #f)
                             (car (cdr rest))
                             (string-length source)))
             (count (- source-end source-start))
             (result (string-copy value)))
        (let loop ((offset 0))
          (if (< offset count)
              (begin
                (string-set! result (+ start offset)
                             (string-ref source (+ source-start offset)))
                (loop (+ offset 1)))))
        result))

    (define (string-replace-from! value start source . rest)
      (let* ((source-start (if (pair? rest) (car rest) 0))
             (source-end (if (if (pair? rest) (pair? (cdr rest)) #f)
                             (car (cdr rest))
                             (string-length source))))
        (let loop ((offset 0))
          (if (< offset (- source-end source-start))
              (begin
                (string-set! value (+ start offset)
                             (string-ref source (+ source-start offset)))
                (loop (+ offset 1)))))))

;;;; Generic sequences

    (define (sequence-size value) (collection-size value))

    (define (sequence-ref value index . rest)
      (cond ((list? value) (apply list-ref value index rest))
            ((vector? value) (apply vector-ref value index rest))
            ((string? value) (apply string-ref value index rest))
            (else (error "sequence-ref" "not a sequence" value))))

    (define (sequence-get-left value . rest)
      (cond ((list? value) (apply list-get-left value rest))
            ((vector? value) (apply vector-get-left value rest))
            ((string? value) (apply string-get-left value rest))
            (else (error "sequence-get-left" "not a sequence" value))))

    (define (sequence-get-right value . rest)
      (cond ((list? value) (apply list-get-right value rest))
            ((vector? value) (apply vector-get-right value rest))
            ((string? value) (apply string-get-right value rest))
            (else (error "sequence-get-right" "not a sequence" value))))))
