; define-record-type (R7RS 5.5). A record type is identified by a unique
; descriptor (a fresh pair holding the type name); instances are BOX_RECORD
; objects, disjoint from every other type. Field positions are assigned at
; expansion time by walking the field specs, so accessors index directly.

(define (make-record-type-descriptor name) (cons name '()))

; Constructor argument positions follow the constructor spec; fields not
; named there start as #f.
(define (record-field-index field fields index)
  (if (null? fields)
      (argument-error "define-record-type" "constructor names an unknown field"
                      field)
      (if (eq? field (car fields))
          index
          (record-field-index field (cdr fields) (raw-add index 1)))))

(define (record-constructor type fields arguments)
  (let ((indexes (map (lambda (field) (record-field-index field fields 1))
                      arguments))
        (count (length fields)))
    (lambda values
      (if (not (raw-equal (length values) (length indexes)))
          (argument-error "record constructor" "wrong number of arguments"
                          (length values)))
      (let ((slots (make-vector count #f)))
        (for-each (lambda (index value)
                    (vector-set! slots (raw-subtract index 1) value))
                  indexes values)
        (apply npl_make_record type (vector->list slots))))))

(define-syntax define-record-type
  (syntax-rules ()
    ((_ type-name (constructor field-arg ...) predicate field-spec ...)
     (begin
       (define type-name (make-record-type-descriptor 'type-name))
       (define constructor
         (record-constructor type-name
                             (record-spec-fields field-spec ...)
                             '(field-arg ...)))
       (define (predicate object) (npl_record_p object type-name))
       (record-field-procedures type-name 1 field-spec ...)))
    ((_ type-name #f predicate field-spec ...)
     (begin
       (define type-name (make-record-type-descriptor 'type-name))
       (define (predicate object) (npl_record_p object type-name))
       (record-field-procedures type-name 1 field-spec ...)))))

(define-syntax record-spec-fields
  (syntax-rules ()
    ((_ (field rest ...) ...) '(field ...))))

; Each field's slot index is an expression built while expanding the field
; list: 1, then (raw-add 1 1), and so on. It is computed once per definition.
(define-syntax record-field-procedures
  (syntax-rules ()
    ((_ type index) (begin))
    ((_ type index (field accessor) more ...)
     (begin
       (define accessor
         (let ((slot index))
           (lambda (record) (npl_record_ref record type slot))))
       (record-field-procedures type (raw-add index 1) more ...)))
    ((_ type index (field accessor modifier) more ...)
     (begin
       (define accessor
         (let ((slot index))
           (lambda (record) (npl_record_ref record type slot))))
       (define modifier
         (let ((slot index))
           (lambda (record value) (npl_record_set record type slot value))))
       (record-field-procedures type (raw-add index 1) more ...)))))
