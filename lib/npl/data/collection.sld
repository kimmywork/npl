; SRFI 44 collections for lists, vectors, and strings, with the canonical NPL
; library name.
;
; The reference SRFI 44 specifies a generic dispatch layer over many
; collection types. This library implements the part the 0.1.2 plan calls for:
; lists, vectors, and strings are collections, directional collections, bags,
; and sequences. Vectors and strings are limited and purely mutable
; collections; lists are flexible sequences. Sets, maps, and dictionaries are
; not part of this subset.
;
; The SRFI 44 forms of list-ref, vector-ref, string-ref, list-copy,
; list-set!, vector-set!, string-set!, and make-list accept optional absence
; and range arguments or return the updated collection, so they are defined
; here rather than re-exported from (scheme base). A combined import with
; (scheme base) therefore needs only/except/prefix for those names.
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
  (import (except (scheme base)
                  list-ref vector-ref string-ref list-copy list-set!
                  vector-set! string-set! make-list)
          (rename (only (scheme base) vector-set! string-set! make-list)
                  (vector-set! base-vector-set!)
                  (string-set! base-string-set!)
                  (make-list base-make-list)))
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

    ; SRFI 44 classifies sequences as directional bags, not as ordered
    ; collections. Vectors and strings are limited and purely mutable; lists
    ; are unbounded flexible sequences.
    (define (ordered-collection? value) #f)
    (define (limited-collection? value)
      (let ((kind (collection-kind value)))
        (if (if kind (not (eq? kind 'list)) #f) #t #f)))
    (define (directional-collection? value) (collection? value))
    (define (purely-mutable-collection? value)
      (let ((kind (collection-kind value)))
        (if (if kind (not (eq? kind 'list)) #f) #t #f)))
    (define (sequence? value) (collection? value))
    (define (flexible-sequence? value) (eq? (collection-kind value) 'list))
    (define (bag? value) (collection? value))
    (define (set? value) #f)
    (define (map? value) #f)
    (define (dictionary? value) #f)

    (define (list-range-copy value start end)
      (let loop ((items (list-tail value start))
                 (remaining (- end start))
                 (result '()))
        (if (or (null? items) (<= remaining 0))
            (reverse result)
            (loop (cdr items) (- remaining 1) (cons (car items) result)))))

    (define (collection->list value)
      (cond ((list? value) (list-range-copy value 0 (length value)))
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
      (if (eq? (collection-kind value) 'string) char=? eqv?))

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
      (cond ((list? value) (collection->list value))
            ((vector? value) (vector-copy value))
            ((string? value) (string-copy value))
            (else (error "collection-copy" "not a collection" value))))

    (define (collection-clear value)
      (cond ((list? value) '())
            ((vector? value) (make-vector 0))
            ((string? value) "")
            (else (error "collection-clear" "not a collection" value))))

    (define (collection-clear! value)
      (if (eq? (collection-kind value) 'list)
          '()
          (error "collection-clear!" "limited collection cannot be cleared")))

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

    ; Sequence keys are indices, paired with the element at that index.
    (define (collection-fold-keys-left value function . seeds)
      (let loop ((items (collection->list value)) (index 0) (state seeds))
        (if (null? items)
            (apply values state)
            (call-with-values
              (lambda () (apply function index (car items) state))
              (lambda (proceed . next)
                (if proceed
                    (loop (cdr items) (+ index 1) next)
                    (apply values next)))))))

    (define (collection-fold-keys-right value function . seeds)
      (let ((items (collection->list value)))
        (let loop ((index (- (length items) 1)) (state seeds))
          (if (< index 0)
              (apply values state)
              (call-with-values
                (lambda () (apply function index
                                  (car (list-tail items index)) state))
                (lambda (proceed . next)
                  (if proceed
                      (loop (- index 1) next)
                      (apply values next))))))))

;;;; Lists

    (define (make-list . rest)
      (if (null? rest) '() (apply base-make-list rest)))

    (define (list->list value) (collection->list value))

    (define (list-size value) (length value))
    (define (list-empty? value) (null? value))
    (define (list-equivalence-function value) eqv?)
    (define (list-contains? value element)
      (if (memv element value) #t #f))

    (define (list-ref value index . rest)
      (if (if (exact-integer? index)
              (if (<= 0 index) (< index (length value)) #f)
              #f)
          (car (list-tail value index))
          (absence-value rest)))

    (define (list-copy value . rest)
      (let* ((start (if (pair? rest) (car rest) 0))
             (end (if (if (pair? rest) (pair? (cdr rest)) #f)
                      (car (cdr rest))
                      (length value))))
        (if (if (exact-integer? start)
                (if (exact-integer? end)
                    (if (<= 0 start end) (<= end (length value)) #f)
                    #f)
                #f)
            (list-range-copy value start end)
            (error "list-copy" "invalid range" start end))))

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

    (define (list-set! value index replacement)
      (if (if (exact-integer? index)
              (if (<= 0 index) (< index (length value)) #f)
              #f)
          (let ((cell (list-tail value index)))
            (set-car! cell replacement)
            value)
          (error "list-set!" "index out of range" index)))

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

    (define (list-delete value element) (delete-first eqv? value element))
    (define (list-delete! value element) (delete-first eqv? value element))
    (define (list-delete-all value element) (delete-all eqv? value element))
    (define (list-delete-all! value element) (delete-all eqv? value element))

    (define (list-delete-left value)
      (if (null? value)
          (values '() #f)
          (values (cdr value) (car value))))
    (define list-delete-left! list-delete-left)

    (define (delete-right value)
      (if (null? value)
          (values '() #f)
          (if (null? (cdr value))
              (values '() (car value))
              (let loop ((items value))
                (if (null? (cdr (cdr items)))
                    (let ((removed (car (cdr items))))
                      (set-cdr! items '())
                      (values value removed))
                    (loop (cdr items)))))))

    (define (list-delete-right value) (delete-right (list-range-copy value 0 (length value))))
    (define list-delete-right! delete-right)

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

;;;; Vectors

    (define (vector-size value) (vector-length value))
    (define (vector-empty? value) (zero? (vector-length value)))
    (define (vector-equivalence-function value) eqv?)

    (define (vector-ref value index . rest)
      (if (if (exact-integer? index)
              (if (<= 0 index) (< index (vector-length value)) #f)
              #f)
          (car (list-tail (vector->list value) index))
          (absence-value rest)))

    (define (vector-contains? value element)
      (if (memv element (vector->list value)) #t #f))
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
               (if (let position-loop ((index 0))
                     (if (>= index (vector-length (car rest)))
                         #t
                         (if (same? (vector-ref (car rest) index)
                                    (vector-ref (car (cdr rest)) index))
                             (position-loop (+ index 1))
                             #f)))
                   (loop (cdr rest))
                   #f))
              (else #f))))

    (define (vector-set value index replacement)
      (let ((result (vector-copy value)))
        (base-vector-set! result index replacement)
        result))

    (define (vector-set! value index replacement)
      (base-vector-set! value index replacement)
      value)

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
                (base-vector-set! result (+ start offset)
                                  (vector-ref source (+ source-start offset)))
                (loop (+ offset 1)))))
        result))

    (define (vector-replace-from! value start source . rest)
      (let* ((source-start (if (pair? rest) (car rest) 0))
             (source-end (if (if (pair? rest) (pair? (cdr rest)) #f)
                             (car (cdr rest))
                             (vector-length source)))
             (count (- source-end source-start))
             (snapshot (let loop ((offset 0) (result '()))
                         (if (>= offset count)
                             (reverse result)
                             (loop (+ offset 1)
                                   (cons (vector-ref source
                                                     (+ source-start offset))
                                         result))))))
        (let loop ((offset 0) (items snapshot))
          (if (< offset count)
              (begin
                (base-vector-set! value (+ start offset) (car items))
                (loop (+ offset 1) (cdr items)))))
        value))

;;;; Strings

    (define (string-size value) (string-length value))
    (define (string-empty? value) (zero? (string-length value)))
    (define (string-equivalence-function value) char=?)

    (define (string-ref value index . rest)
      (if (if (exact-integer? index)
              (if (<= 0 index) (< index (string-length value)) #f)
              #f)
          (car (list-tail (string->list value) index))
          (absence-value rest)))

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
        (base-string-set! result index replacement)
        result))

    (define (string-set! value index replacement)
      (base-string-set! value index replacement)
      value)

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
                (base-string-set! result (+ start offset)
                                  (string-ref source (+ source-start offset)))
                (loop (+ offset 1)))))
        result))

    (define (string-replace-from! value start source . rest)
      (let* ((source-start (if (pair? rest) (car rest) 0))
             (source-end (if (if (pair? rest) (pair? (cdr rest)) #f)
                             (car (cdr rest))
                             (string-length source)))
             (count (- source-end source-start))
             (snapshot (let loop ((offset 0) (result '()))
                         (if (>= offset count)
                             (reverse result)
                             (loop (+ offset 1)
                                   (cons (string-ref source
                                                     (+ source-start offset))
                                         result))))))
        (let loop ((offset 0) (items snapshot))
          (if (< offset count)
              (begin
                (base-string-set! value (+ start offset) (car items))
                (loop (+ offset 1) (cdr items)))))
        value))

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
