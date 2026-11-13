; SRFI 41 stream primitives.
;
; Port of the SRFI 41 reference implementation by Philip L. Bewig (MIT).
; A stream is a record holding one mutable box. The box contains either
; (lazy . thunk) or (eager . value). stream-cons stores the head and tail as
; separate lazy streams, so forcing a stream never evaluates either field.
(define-record-type stream-type
  (make-stream box)
  stream?
  (box stream-promise stream-promise!))

(define-syntax stream-lazy
  (syntax-rules ()
    ((_ expression)
     (make-stream (cons 'lazy (lambda () expression))))))

(define (stream-eager expression)
  (make-stream (cons 'eager expression)))

(define-syntax stream-delay
  (syntax-rules ()
    ((_ expression) (stream-lazy (stream-eager expression)))))

(define (stream-force promise)
  (let ((content (stream-promise promise)))
    (case (car content)
      ((eager) (cdr content))
      ((lazy)
       (let* ((promise* ((cdr content)))
              (content (stream-promise promise)))
         (if (not (eqv? (car content) 'eager))
             (begin
               (set-car! content (car (stream-promise promise*)))
               (set-cdr! content (cdr (stream-promise promise*)))
               (stream-promise! promise* content)))
         (stream-force promise))))))

(define stream-null (stream-delay (cons 'stream 'null)))

(define-record-type stream-pare-type
  (make-stream-pare kar kdr)
  stream-pare?
  (kar stream-kar)
  (kdr stream-kdr))

(define (stream-pair? object)
  (and (stream? object) (stream-pare? (stream-force object))))

(define (stream-null? object)
  (and (stream? object)
       (eqv? (stream-force object) (stream-force stream-null))))

(define-syntax stream-cons
  (syntax-rules ()
    ((_ object stream)
     (stream-eager (make-stream-pare (stream-delay object)
                                     (stream-lazy stream))))))

(define (stream-car stream)
  (cond ((not (stream? stream)) (error "stream-car: non-stream" stream))
        ((stream-null? stream) (error "stream-car: null stream"))
        (else (stream-force (stream-kar (stream-force stream))))))

(define (stream-cdr stream)
  (cond ((not (stream? stream)) (error "stream-cdr: non-stream" stream))
        ((stream-null? stream) (error "stream-cdr: null stream"))
        (else (stream-kdr (stream-force stream)))))

(define-syntax stream-lambda
  (syntax-rules ()
    ((_ formals body ...)
     (lambda formals (stream-lazy (let () body ...))))))
