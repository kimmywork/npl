; Record, fallible file-open, read-result, and port primitives for the
; evaluator. Records are
; host runtime records, so (scheme base) define-record-type works unchanged.

(define (eval-record-of-type? value type)
  (npl_record_p value type))

(define (eval-apply-record-primitive name arguments continuation)
  (cond
    ((string=? name "npl_make_record")
     (if (pair? arguments)
         (continuation (apply npl_make_record arguments))
         (continuation (eval-error "record: needs a type"))))
    ((string=? name "npl_record_p")
     (let ((error (eval-arity arguments 2 name)))
       (if error (continuation error)
           (continuation
             (eval-record-of-type? (car arguments) (car (cdr arguments)))))))
    ((string=? name "npl_record_ref")
     (let ((error (eval-arity arguments 3 name)))
       (if error (continuation error)
           (if (eval-record-of-type? (car arguments) (car (cdr arguments)))
               (continuation (apply npl_record_ref arguments))
               (continuation
                 (eval-error "record accessor: wrong record type"))))))
    ((string=? name "npl_record_set")
     (let ((error (eval-arity arguments 4 name)))
       (if error (continuation error)
           (if (eval-record-of-type? (car arguments) (car (cdr arguments)))
               (begin (apply npl_record_set arguments)
                      (continuation EVAL-UNSPECIFIED))
               (continuation
                 (eval-error "record modifier: wrong record type"))))))
    ((string=? name "npl_try_open_file")
     (let ((error (eval-arity arguments 2 name)))
       (if error (continuation error)
           (eval-host-primitive (lambda () (apply npl_try_open_file arguments))
                                continuation))))
    ((string=? name "read-result")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (eval-host-primitive (lambda () (read-result (car arguments)))
                                continuation))))
    ((string=? name "current-input-port")
     (let ((error (eval-arity arguments 0 name)))
       (if error (continuation error)
           (continuation (eval-current-port 0)))))
    ((string=? name "npl_port_parameter")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (if (memv (car arguments) '(0 1 2))
               (continuation
                 (vector-ref EVAL-PORT-PARAMETERS (car arguments)))
               (eval-primitive-error "port parameter" "invalid port kind"
                                     (car arguments) continuation)))))
    ((string=? name "file-exists?")
     (let ((error (eval-arity arguments 1 name)))
       (if error (continuation error)
           (eval-host-primitive (lambda () (file-exists? (car arguments)))
                                continuation))))
    (else (eval-apply-port-primitive name arguments continuation))))

;; Port and binary I/O primitives: the host primitive checks its arguments
;; and its error object is re-raised through the evaluator. Procedures with
;; an optional port default to the evaluator's current port parameters.
(define (eval-port-call arguments required kind procedure continuation)
  (eval-host-primitive
    (lambda ()
      (apply procedure
             (if (= (length arguments) required)
                 (append arguments (list (eval-current-port kind)))
                 arguments)))
    continuation))

(define (eval-apply-port-primitive name arguments continuation)
  (let ((host
          (lambda (procedure)
            (eval-host-primitive (lambda () (apply procedure arguments))
                                 continuation))))
    (cond
      ((string=? name "input-port?") (host input-port?))
      ((string=? name "output-port?") (host output-port?))
      ((string=? name "textual-port?") (host textual-port?))
      ((string=? name "binary-port?") (host binary-port?))
      ((string=? name "port?") (host port?))
      ((string=? name "eof-object") (host eof-object))
      ((string=? name "npl_port_ready_p") (host npl_port_ready_p))
      ((string=? name "npl_jiffies_per_second") (host npl_jiffies_per_second))
      ((string=? name "npl_current_jiffy") (host npl_current_jiffy))
      ((string=? name "npl_current_second") (host npl_current_second))
      ((string=? name "close-output-port")
       (eval-host-primitive
         (lambda () (apply close-output-port arguments) EVAL-UNSPECIFIED)
         continuation))
      ((string=? name "open-input-bytevector") (host open-input-bytevector))
      ((string=? name "open-output-bytevector") (host open-output-bytevector))
      ((string=? name "get-output-bytevector") (host get-output-bytevector))
      ((string=? name "open-binary-input-file") (host open-binary-input-file))
      ((string=? name "open-binary-output-file") (host open-binary-output-file))
      ((string=? name "read-bytevector") (host read-bytevector))
      ((string=? name "read-bytevector!") (host read-bytevector!))
      ((string=? name "write-bytevector")
       (eval-host-primitive
         (lambda () (apply write-bytevector arguments) EVAL-UNSPECIFIED)
         continuation))
      ((string=? name "peek-char")
       (eval-port-call arguments 0 0 peek-char continuation))
      ((string=? name "read-u8")
       (eval-port-call arguments 0 0 read-u8 continuation))
      ((string=? name "peek-u8")
       (eval-port-call arguments 0 0 peek-u8 continuation))
      ((string=? name "write-u8")
       (eval-host-primitive
         (lambda ()
           (eval-port-call-now arguments 1 write-u8)
           EVAL-UNSPECIFIED)
         continuation))
      ((string=? name "flush-output-port")
       (eval-host-primitive
         (lambda ()
           (eval-port-call-now arguments 0 flush-output-port)
           EVAL-UNSPECIFIED)
         continuation))
      ((string=? name "write-shared")
       (eval-host-primitive
         (lambda () (eval-port-call-now arguments 1 write-shared)
                    EVAL-UNSPECIFIED)
         continuation))
      ((string=? name "write-simple")
       (eval-host-primitive
         (lambda () (eval-port-call-now arguments 1 write-simple)
                    EVAL-UNSPECIFIED)
         continuation))
      (else
       (continuation
         (eval-error (string-append "unknown primitive: " name)))))))

;; Output procedures with an optional trailing port, called directly.
(define (eval-port-call-now arguments required procedure)
  (apply procedure
         (if (= (length arguments) required)
             (append arguments (list (eval-current-port 1)))
             arguments)))
