; (srfi 125) procedures; see 125.sld. Runtime kinds (runtime/npl.h):
; 0 eq?, 1 eqv?, 2 equal?, 3 string=?, 4 general.

(define (table-error who message . irritants)
  (apply error (string-append who ": " message) irritants))

(define (equality-kind equivalence)
  (cond
    ((npl_same_procedure_p equivalence eq?) 0)
    ((npl_same_procedure_p equivalence eqv?) 1)
    ((npl_same_procedure_p equivalence equal?) 2)
    ((npl_same_procedure_p equivalence string=?) 3)
    (else 4)))

; The hash function that goes with a predicate the general kind serves.
(define (default-hash-function equivalence)
  (if (npl_same_procedure_p equivalence string-ci=?) string-ci-hash #f))

; Leading procedure arguments are the predicate and the hash function; an
; exact non-negative integer among the rest is the initial capacity, other
; arguments (weak-keys and so on) are ignored.
(define (table-capacity args)
  (cond
    ((null? args) 0)
    ((and (exact-integer? (car args)) (>= (car args) 0)) (car args))
    (else (table-capacity (cdr args)))))

(define (new-table who equivalence args)
  (if (not (procedure? equivalence))
      (table-error who "expected an equality predicate (comparators need SRFI 128)"
                   equivalence))
  (let* ((hash-function (if (and (pair? args) (procedure? (car args)))
                            (car args)
                            #f))
         (rest (if hash-function (cdr args) args))
         (kind (equality-kind equivalence))
         (capacity (table-capacity rest)))
    (if (= kind 4)
        (let ((hash-function
                (or hash-function (default-hash-function equivalence))))
          (if (not hash-function)
              (table-error who "unknown equality predicate needs a hash function"
                           equivalence))
          (npl_make_hash_table 4 equivalence hash-function capacity))
        (npl_make_hash_table kind equivalence hash-function capacity))))

(define (make-hash-table . args)
  (if (null? args)
      (npl_make_hash_table 2 equal? #f 0)
      (new-table "make-hash-table" (car args) (cdr args))))

(define (set-pairs! who table pairs)
  (cond
    ((null? pairs) table)
    ((null? (cdr pairs)) (table-error who "odd number of keys and values"))
    (else
     (npl_hash_table_set table (car pairs) (car (cdr pairs)))
     (set-pairs! who table (cdr (cdr pairs))))))

(define (hash-table equivalence . pairs)
  (let ((table (new-table "hash-table" equivalence '())))
    (set-pairs! "hash-table" table pairs)
    (npl_hash_table_copy table #f)))

(define (hash-table-unfold stop? mapper successor seed equivalence . args)
  (let ((table (new-table "hash-table-unfold" equivalence args)))
    (let loop ((seed seed))
      (if (stop? seed)
          table
          (call-with-values
            (lambda () (mapper seed))
            (lambda (key value)
              (npl_hash_table_set table key value)
              (loop (successor seed))))))))

; Earlier associations take precedence.
(define (alist->hash-table alist . args)
  (let ((table (if (null? args)
                   (make-hash-table)
                   (new-table "alist->hash-table" (car args) (cdr args)))))
    (for-each (lambda (association)
                (if (not (npl_hash_table_contains_p table (car association)))
                    (npl_hash_table_set table (car association)
                                        (cdr association))))
              alist)
    table))

(define (hash-table? object) (npl_hash_table_p object))
(define (hash-table-contains? table key) (npl_hash_table_contains_p table key))
(define hash-table-exists? hash-table-contains?)
(define (hash-table-empty? table) (= (npl_hash_table_size table) 0))
(define (hash-table-mutable? table) (npl_hash_table_info table 3))

(define missing (list 'missing))

; The value comparator is an equality predicate here (no SRFI 128).
(define (hash-table=? same-value? left right)
  (and (= (npl_hash_table_size left) (npl_hash_table_size right))
       (let ((items (npl_hash_table_items left)))
         (let loop ((index 0))
           (if (= index (vector-length items))
               #t
               (let ((other (npl_hash_table_ref
                              right (vector-ref items index) missing)))
                 (and (not (eq? other missing))
                      (same-value? (vector-ref items (+ index 1)) other)
                      (loop (+ index 2)))))))))

(define (hash-table-ref table key . procedures)
  (let ((value (npl_hash_table_ref table key missing)))
    (cond
      ((not (eq? value missing))
       (if (and (pair? procedures) (pair? (cdr procedures)))
           ((car (cdr procedures)) value)
           value))
      ((pair? procedures) ((car procedures)))
      (else (table-error "hash-table-ref" "key not found" key)))))

(define (hash-table-ref/default table key default)
  (npl_hash_table_ref table key default))

(define (hash-table-set! table . pairs)
  (set-pairs! "hash-table-set!" table pairs)
  (if #f #f))

(define (hash-table-delete! table . keys)
  (let loop ((keys keys) (count 0))
    (if (null? keys)
        count
        (loop (cdr keys) (+ count (npl_hash_table_delete table (car keys)))))))

(define (hash-table-intern! table key failure)
  (let ((value (npl_hash_table_ref table key missing)))
    (if (eq? value missing)
        (let ((value (failure)))
          (npl_hash_table_set table key value)
          value)
        value)))

(define (hash-table-update! table key updater . procedures)
  (npl_hash_table_set table key
    (updater (apply hash-table-ref table key procedures))))

(define (hash-table-update!/default table key updater default)
  (npl_hash_table_set table key
    (updater (npl_hash_table_ref table key default))))

(define (hash-table-pop! table)
  (let ((association (npl_hash_table_pop table)))
    (values (car association) (cdr association))))

(define (hash-table-clear! table) (npl_hash_table_clear table))
(define (hash-table-size table) (npl_hash_table_size table))
(define (hash-table-keys table) (npl_hash_table_keys table))
(define (hash-table-values table) (npl_hash_table_values table))

; Walk a snapshot of the associations: (proc key value) for each.
(define (walk-items table proc)
  (let ((items (npl_hash_table_items table)))
    (let loop ((index 0))
      (if (< index (vector-length items))
          (begin
            (proc (vector-ref items index) (vector-ref items (+ index 1)))
            (loop (+ index 2)))))))

(define (hash-table-entries table)
  (let ((items (npl_hash_table_items table)))
    (let loop ((index (- (vector-length items) 2)) (keys '()) (values* '()))
      (if (< index 0)
          (values keys values*)
          (loop (- index 2)
                (cons (vector-ref items index) keys)
                (cons (vector-ref items (+ index 1)) values*))))))

(define (hash-table-find proc table failure)
  (let ((items (npl_hash_table_items table)))
    (let loop ((index 0))
      (if (= index (vector-length items))
          (failure)
          (or (proc (vector-ref items index) (vector-ref items (+ index 1)))
              (loop (+ index 2)))))))

(define (hash-table-count pred table)
  (let ((count 0))
    (walk-items table
      (lambda (key value) (if (pred key value) (set! count (+ count 1)))))
    count))

(define (hash-table-map proc equivalence table)
  (let ((result (new-table "hash-table-map" equivalence '())))
    (walk-items table
      (lambda (key value) (npl_hash_table_set result key (proc value))))
    result))

(define (hash-table-for-each proc table)
  (walk-items table proc))

(define (hash-table-walk table proc)
  (walk-items table proc))

(define (hash-table-map! proc table)
  (walk-items table
    (lambda (key value) (npl_hash_table_set table key (proc key value)))))

(define (hash-table-map->list proc table)
  (let ((result '()))
    (walk-items table
      (lambda (key value) (set! result (cons (proc key value) result))))
    result))

; (hash-table-fold proc seed table), or the SRFI 69 order (table proc seed).
(define (hash-table-fold first second third)
  (let ((proc (if (hash-table? first) second first))
        (seed (if (hash-table? first) third second))
        (table (if (hash-table? first) first third)))
    (walk-items table
      (lambda (key value) (set! seed (proc key value seed))))
    seed))

(define (hash-table-prune! proc table)
  (walk-items table
    (lambda (key value)
      (if (proc key value) (npl_hash_table_delete table key)))))

(define (hash-table-copy table . mutable)
  (npl_hash_table_copy table (and (pair? mutable) (car mutable))))

(define (hash-table-empty-copy table)
  (npl_make_hash_table (npl_hash_table_info table 0)
                       (npl_hash_table_info table 1)
                       (npl_hash_table_info table 2)
                       0))

(define (hash-table->alist table)
  (hash-table-map->list cons table))

; Values of table1 win for keys in both.
(define (hash-table-union! table1 table2)
  (walk-items table2
    (lambda (key value)
      (if (not (npl_hash_table_contains_p table1 key))
          (npl_hash_table_set table1 key value))))
  table1)

(define hash-table-merge! hash-table-union!)

(define (hash-table-intersection! table1 table2)
  (walk-items table1
    (lambda (key value)
      (if (not (npl_hash_table_contains_p table2 key))
          (npl_hash_table_delete table1 key))))
  table1)

(define (hash-table-difference! table1 table2)
  (walk-items table1
    (lambda (key value)
      (if (npl_hash_table_contains_p table2 key)
          (npl_hash_table_delete table1 key))))
  table1)

(define (hash-table-xor! table1 table2)
  (walk-items table2
    (lambda (key value)
      (if (npl_hash_table_contains_p table1 key)
          (npl_hash_table_delete table1 key)
          (npl_hash_table_set table1 key value))))
  table1)

; The optional second argument of the SRFI 69 hash functions is ignored.
(define (hash object . bound) (npl_hash_value 2 object))
(define (string-hash string . bound) (npl_hash_value 3 string))
(define (string-ci-hash string . bound)
  (npl_hash_value 3 (string-foldcase string)))
(define (hash-by-identity object . bound) (npl_hash_value 0 object))

(define (hash-table-equivalence-function table) (npl_hash_table_info table 1))

; The hash function given at construction (the built-in kinds hash in C and
; do not call it), or #f.
(define (hash-table-hash-function table) (npl_hash_table_info table 2))
