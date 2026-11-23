; C pointers, explicit foreign memory, and deterministic release
; (docs/ffi.md).
;
; A pointer is an opaque C address: pointer? tests for one, pointer-null?
; for the null address, eqv? compares addresses, and write shows
; #<pointer 0x...>. foreign-alloc owns a malloc'd block; foreign-free
; releases it. pointer+offset returns a view that shares the allocation
; but is not itself freeable. The collector never scans foreign memory.
(define-library (npl ffi)
  (export pointer? pointer-null? null-pointer pointer->integer with-resource
          foreign-alloc foreign-free foreign-size foreign-copy
          pointer+offset pointer-offset
          pointer-ref pointer-set!
          pointer-ref-u8 pointer-ref-u16 pointer-ref-u32 pointer-ref-u64
          pointer-ref-i8 pointer-ref-i16 pointer-ref-i32 pointer-ref-i64
          pointer-ref-float pointer-ref-double pointer-ref-pointer
          pointer-set-u8! pointer-set-u16! pointer-set-u32! pointer-set-u64!
          pointer-set-i8! pointer-set-i16! pointer-set-i32! pointer-set-i64!
          pointer-set-float! pointer-set-double! pointer-set-pointer!
          c-type-size
          make-c-struct c-struct? c-struct-pointer c-struct-ref
          c-struct-set! c-struct-size
          c-array? c-array c-array-pointer c-array-length c-array-type
          make-c-array c-array-ref c-array-set! c-array->vector
          vector->c-array)
  (import (scheme base)
          (only (npl internal primitive)
                pointer? pointer-null? null-pointer pointer->integer
                npl_ffi_alloc npl_ffi_foreign_free npl_ffi_foreign_size
                npl_ffi_foreign_copy npl_ffi_pointer_add
                npl_ffi_pointer_offset npl_ffi_pointer_ref
                npl_ffi_pointer_set npl_ffi_type_size))
  (begin
    (define-syntax with-resource
      (syntax-rules ()
        ((_ (name init release) body1 body2 ...)
         (let ((name init) (open #t))
           (dynamic-wind
             (lambda () #f)
             (lambda () body1 body2 ...)
             (lambda ()
               (if open
                   (begin (set! open #f) (release name)))))))))

    ; Type codes are shared with runtime/ffi-memory.c.
    (define (ffi-type-code type)
      (cond
        ((eq? type 'u8) 0)
        ((eq? type 'u16) 1)
        ((eq? type 'u32) 2)
        ((eq? type 'u64) 3)
        ((eq? type 'size_t) 3)
        ((eq? type 'int64) 7)
        ((eq? type 'i8) 4)
        ((eq? type 'i16) 5)
        ((eq? type 'i32) 6)
        ((eq? type 'int) 6)
        ((eq? type 'i64) 7)
        ((eq? type 'long) 7)
        ((eq? type 'float) 8)
        ((eq? type 'double) 9)
        ((eq? type 'pointer) 10)
        ((eq? type 'bool) 0)
        (else (error "npl ffi: unknown C type" type))))

    (define (c-type-size type)
      (npl_ffi_type_size (ffi-type-code type)))

    (define (foreign-alloc size . rest)
      (npl_ffi_alloc size (if (null? rest) 16 (car rest))))

    (define (foreign-copy pointer size . rest)
      (npl_ffi_foreign_copy pointer size (if (null? rest) 16 (car rest))))

    (define (foreign-free pointer)
      (npl_ffi_foreign_free pointer))

    (define (foreign-size pointer)
      (npl_ffi_foreign_size pointer))

    (define (pointer+offset pointer offset)
      (npl_ffi_pointer_add pointer offset))

    (define (pointer-offset pointer)
      (npl_ffi_pointer_offset pointer))

    (define (pointer-ref pointer type . rest)
      (npl_ffi_pointer_ref pointer (ffi-type-code type)
                           (if (null? rest) 0 (car rest))))

    (define (pointer-set! pointer type value . rest)
      (npl_ffi_pointer_set pointer (ffi-type-code type)
                           (if (null? rest) 0 (car rest)) value))

    (define (pointer-ref-u8 pointer . rest)
      (apply pointer-ref pointer 'u8 rest))
    (define (pointer-ref-u16 pointer . rest)
      (apply pointer-ref pointer 'u16 rest))
    (define (pointer-ref-u32 pointer . rest)
      (apply pointer-ref pointer 'u32 rest))
    (define (pointer-ref-u64 pointer . rest)
      (apply pointer-ref pointer 'u64 rest))
    (define (pointer-ref-i8 pointer . rest)
      (apply pointer-ref pointer 'i8 rest))
    (define (pointer-ref-i16 pointer . rest)
      (apply pointer-ref pointer 'i16 rest))
    (define (pointer-ref-i32 pointer . rest)
      (apply pointer-ref pointer 'i32 rest))
    (define (pointer-ref-i64 pointer . rest)
      (apply pointer-ref pointer 'i64 rest))
    (define (pointer-ref-float pointer . rest)
      (apply pointer-ref pointer 'float rest))
    (define (pointer-ref-double pointer . rest)
      (apply pointer-ref pointer 'double rest))
    (define (pointer-ref-pointer pointer . rest)
      (apply pointer-ref pointer 'pointer rest))

    (define (pointer-set-u8! pointer value . rest)
      (apply pointer-set! pointer 'u8 value rest))
    (define (pointer-set-u16! pointer value . rest)
      (apply pointer-set! pointer 'u16 value rest))
    (define (pointer-set-u32! pointer value . rest)
      (apply pointer-set! pointer 'u32 value rest))
    (define (pointer-set-u64! pointer value . rest)
      (apply pointer-set! pointer 'u64 value rest))
    (define (pointer-set-i8! pointer value . rest)
      (apply pointer-set! pointer 'i8 value rest))
    (define (pointer-set-i16! pointer value . rest)
      (apply pointer-set! pointer 'i16 value rest))
    (define (pointer-set-i32! pointer value . rest)
      (apply pointer-set! pointer 'i32 value rest))
    (define (pointer-set-i64! pointer value . rest)
      (apply pointer-set! pointer 'i64 value rest))
    (define (pointer-set-float! pointer value . rest)
      (apply pointer-set! pointer 'float value rest))
    (define (pointer-set-double! pointer value . rest)
      (apply pointer-set! pointer 'double value rest))
    (define (pointer-set-pointer! pointer value . rest)
      (apply pointer-set! pointer 'pointer value rest))

    (define-record-type c-struct-record
      (make-c-struct-record pointer descriptor)
      c-struct?
      (pointer c-struct-pointer)
      (descriptor c-struct-descriptor))

    (define (c-struct-descriptor? descriptor)
      (if (pair? descriptor)
          (if (assq 'size descriptor)
              (if (assq 'fields descriptor) #t #f)
              #f)
          #f))

    (define (make-c-struct descriptor)
      (if (not (c-struct-descriptor? descriptor))
          (error "make-c-struct: expected a define-c-struct descriptor"
                 descriptor))
      (make-c-struct-record
        (foreign-alloc (cdr (assq 'size descriptor)))
        descriptor))

    (define (c-struct-size view)
      (if (not (c-struct? view))
          (error "c-struct-size: expected a C struct view" view))
      (cdr (assq 'size (c-struct-descriptor view))))

    (define (c-struct-field descriptor field)
      (let ((entry (assq field (cdr (assq 'fields descriptor)))))
        (if entry
            entry
            (error "C struct has no such field" field))))

    (define (c-struct-ref view field)
      (if (not (c-struct? view))
          (error "c-struct-ref: expected a C struct view" view))
      (let ((entry (c-struct-field (c-struct-descriptor view) field)))
        (npl_ffi_pointer_ref (c-struct-pointer view)
                             (car (cdr (cdr entry)))
                             (car (cdr entry)))))

    (define (c-struct-set! view field value)
      (if (not (c-struct? view))
          (error "c-struct-set!: expected a C struct view" view))
      (let ((entry (c-struct-field (c-struct-descriptor view) field)))
        (npl_ffi_pointer_set (c-struct-pointer view)
                             (car (cdr (cdr entry)))
                             (car (cdr entry))
                             value)))

    (define-record-type c-array-record
      (make-c-array-record pointer length type)
      c-array?
      (pointer c-array-pointer)
      (length c-array-length)
      (type c-array-type))

    (define (c-array pointer length type)
      (if (not (pointer? pointer))
          (error "c-array: expected a pointer" pointer))
      (if (not (if (exact-integer? length) (>= length 0) #f))
          (error "c-array: expected a non-negative length" length))
      (ffi-type-code type)
      (make-c-array-record pointer length type))

    (define (make-c-array length type . rest)
      (let ((size (* length (c-type-size type))))
        (c-array (apply foreign-alloc size rest) length type)))

    (define (c-array-ref array index)
      (if (not (c-array? array))
          (error "c-array-ref: expected a c-array" array))
      (if (not (if (exact-integer? index)
                   (if (>= index 0) (< index (c-array-length array)) #f)
                   #f))
          (error "c-array-ref: index out of bounds" index))
      (pointer-ref (c-array-pointer array) (c-array-type array)
                   (* index (c-type-size (c-array-type array)))))

    (define (c-array-set! array index value)
      (if (not (c-array? array))
          (error "c-array-set!: expected a c-array" array))
      (if (not (if (exact-integer? index)
                   (if (>= index 0) (< index (c-array-length array)) #f)
                   #f))
          (error "c-array-set!: index out of bounds" index))
      (pointer-set! (c-array-pointer array) (c-array-type array) value
                    (* index (c-type-size (c-array-type array)))))

    (define (c-array->vector array)
      (if (not (c-array? array))
          (error "c-array->vector: expected a c-array" array))
      (let ((result (make-vector (c-array-length array))))
        (let loop ((index 0))
          (if (= index (c-array-length array))
              result
              (begin
                (vector-set! result index (c-array-ref array index))
                (loop (+ index 1)))))))

    (define (vector->c-array vector type . rest)
      (let* ((length (vector-length vector))
             (array (if (null? rest)
                        (make-c-array length type)
                        (c-array (car rest) length type))))
        (let loop ((index 0))
          (if (= index length)
              array
              (begin
                (c-array-set! array index (vector-ref vector index))
                (loop (+ index 1)))))))))
