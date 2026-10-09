; Error objects (R7RS 6.11). An error object is a runtime record whose type
; descriptor is the interned symbol npl-error-object (record types made by
; define-record-type use fresh pairs, so they never collide). Slots:
; 1 kind (error, file, read), 2 message, 3 irritants. (scheme file),
; (scheme read), and runtime primitive errors build the same layout.

(define (error message . irritants)
  (if (not (string? message)) (raise "error: message must be a string"))
  (raise (npl_make_record 'npl-error-object 'error message irritants)))

(define (error-object? object) (npl_record_p object 'npl-error-object))

(define (error-object-field object index who)
  (if (error-object? object)
      (npl_record_ref object 'npl-error-object index)
      (raise (string-append who ": expected an error object"))))

(define (error-object-message object)
  (error-object-field object 2 "error-object-message"))

(define (error-object-irritants object)
  (error-object-field object 3 "error-object-irritants"))

(define (file-error? object)
  (if (error-object? object)
      (eq? (npl_record_ref object 'npl-error-object 1) 'file)
      #f))

(define (read-error? object)
  (if (error-object? object)
      (eq? (npl_record_ref object 'npl-error-object 1) 'read)
      #f))
