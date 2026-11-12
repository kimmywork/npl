; SRFI 41 derived stream operations.
;
; Port of the SRFI 41 reference implementation by Philip L. Bewig (MIT),
; with a syntax-rules stream-match instead of syntax-case.

(define (stream-any pred items)
  (if (null? items) #f (if (pred (car items)) #t (stream-any pred (cdr items)))))

(define-syntax define-stream
  (syntax-rules ()
    ((_ (name . formals) body ...)
     (define name
       (letrec ((name (stream-lambda formals body ...))) name)))))

(define (list->stream objects)
  (if (not (list? objects))
      (error "list->stream: non-list argument" objects)
      (let loop ((items objects))
        (if (null? items)
            stream-null
            (stream-cons (car items) (loop (cdr items)))))))

(define (port->stream . rest)
  (let ((port (if (pair? rest) (car rest) (current-input-port))))
    (if (not (input-port? port))
        (error "port->stream: non-input-port argument" port)
        (let loop ()
          (let ((c (read-char port)))
            (if (eof-object? c)
                stream-null
                (stream-cons c (loop))))))))

(define-syntax stream
  (syntax-rules ()
    ((_) stream-null)
    ((_ x y ...) (stream-cons x (stream y ...)))))

(define (stream->list . args)
  (let ((count (if (= 1 (length args)) #f (car args)))
        (stream (if (= 1 (length args)) (car args) (cadr args))))
    (cond ((not (stream? stream))
           (error "stream->list: non-stream argument" stream))
          ((and count (not (integer? count)))
           (error "stream->list: non-integer count" count))
          ((and count (negative? count))
           (error "stream->list: negative count" count))
          (else
           (let loop ((left (if count count -1)) (stream stream))
             (if (or (zero? left) (stream-null? stream))
                 '()
                 (cons (stream-car stream)
                       (loop (- left 1) (stream-cdr stream)))))))))

(define (stream-append . streams)
  (cond ((null? streams) stream-null)
        ((stream-any (lambda (item) (not (stream? item))) streams)
         (error "stream-append: non-stream argument"))
        (else
         (letrec
             ((append-stream
               (stream-lambda (items)
                 (cond ((null? (cdr items)) (car items))
                       ((stream-null? (car items))
                        (append-stream (cdr items)))
                       (else
                        (stream-cons
                         (stream-car (car items))
                         (append-stream
                          (cons (stream-cdr (car items)) (cdr items)))))))))
           (append-stream streams)))))

(define (stream-concat streams)
  (if (not (stream? streams))
      (error "stream-concat: non-stream argument" streams)
      (letrec
          ((concat-stream
            (stream-lambda (streams)
              (cond ((stream-null? streams) stream-null)
                    ((not (stream? (stream-car streams)))
                     (error "stream-concat: non-stream object in input"))
                    ((stream-null? (stream-car streams))
                     (concat-stream (stream-cdr streams)))
                    (else
                     (stream-cons
                      (stream-car (stream-car streams))
                      (concat-stream
                       (stream-cons (stream-cdr (stream-car streams))
                                    (stream-cdr streams)))))))))
        (concat-stream streams))))

(define (stream-constant . objects)
  (if (null? objects)
      stream-null
      (letrec
          ((constant-stream
            (stream-lambda (items)
              (if (null? (cdr items))
                  (stream-cons (car items) (constant-stream items))
                  (stream-cons (car items)
                               (constant-stream
                                (append (cdr items) (list (car items)))))))))
        (constant-stream objects))))

(define (stream-drop count stream)
  (cond ((not (integer? count)) (error "stream-drop: non-integer" count))
        ((negative? count) (error "stream-drop: negative" count))
        ((not (stream? stream)) (error "stream-drop: non-stream" stream))
        (else
         (letrec
             ((drop-stream
               (stream-lambda (left stream)
                 (if (or (zero? left) (stream-null? stream))
                     stream
                     (drop-stream (- left 1) (stream-cdr stream))))))
           (drop-stream count stream)))))

(define (stream-drop-while pred stream)
  (if (not (stream? stream))
      (error "stream-drop-while: non-stream" stream)
      (letrec
          ((drop-stream
            (stream-lambda (stream)
              (if (and (stream-pair? stream) (pred (stream-car stream)))
                  (drop-stream (stream-cdr stream))
                  stream))))
        (drop-stream stream))))

(define (stream-filter pred stream)
  (if (not (stream? stream))
      (error "stream-filter: non-stream" stream)
      (letrec
          ((filter-stream
            (stream-lambda (stream)
              (cond ((stream-null? stream) stream-null)
                    ((pred (stream-car stream))
                     (stream-cons (stream-car stream)
                                  (filter-stream (stream-cdr stream))))
                    (else (filter-stream (stream-cdr stream)))))))
        (filter-stream stream))))

(define (stream-fold proc base stream)
  (if (not (stream? stream))
      (error "stream-fold: non-stream" stream)
      (let loop ((state base) (stream stream))
        (if (stream-null? stream)
            state
            (loop (proc state (stream-car stream))
                  (stream-cdr stream))))))

(define (stream-for-each proc . streams)
  (if (or (null? streams)
          (stream-any (lambda (stream) (not (stream? stream))) streams))
      (error "stream-for-each: invalid stream argument")
      (let loop ((streams streams))
        (if (not (stream-any stream-null? streams))
            (begin
              (apply proc (map stream-car streams))
              (loop (map stream-cdr streams)))))))

(define (stream-from first . rest)
  (let ((step (if (pair? rest) (car rest) 1)))
    (let loop ((value first))
      (stream-cons value (loop (+ value step))))))

(define (stream-iterate proc base)
  (stream-cons base (stream-iterate proc (proc base))))

(define (stream-length stream)
  (if (not (stream? stream))
      (error "stream-length: non-stream" stream)
      (let loop ((count 0) (stream stream))
        (if (stream-null? stream)
            count
            (loop (+ count 1) (stream-cdr stream))))))

(define-syntax stream-let
  (syntax-rules ()
    ((_ tag ((name value) ...) body ...)
     ((letrec ((tag (stream-lambda (name ...) body ...))) tag)
      value ...))))

(define (stream-map proc . streams)
  (if (or (null? streams)
          (stream-any (lambda (stream) (not (stream? stream))) streams))
      (error "stream-map: invalid stream argument")
      (letrec
          ((map-streams
            (stream-lambda (streams)
              (if (stream-any stream-null? streams)
                  stream-null
                  (stream-cons
                   (apply proc (map stream-car streams))
                   (map-streams (map stream-cdr streams)))))))
        (map-streams streams))))

(define-syntax stream-match
  (syntax-rules ()
    ((_ expression clause ...)
     (let ((stream expression))
       (if (stream? stream)
           (stream-match-clauses stream clause ...)
           (error "stream-match: non-stream argument"))))))

(define-syntax stream-match-clauses
  (syntax-rules ()
    ((_ stream)
     (error "stream-match: pattern failure"))
    ((_ stream (pattern fender expression) clause ...)
     (stream-match-pattern
      stream pattern ()
      (if fender expression (stream-match-clauses stream clause ...))
      (stream-match-clauses stream clause ...)))
    ((_ stream (pattern expression) clause ...)
     (stream-match-pattern stream pattern () expression
                           (stream-match-clauses stream clause ...)))))

(define-syntax stream-match-pattern
  (syntax-rules (_)
    ((_ stream () bindings body fail)
     (if (stream-null? stream) (let bindings body) fail))
    ((_ stream (_ . rest) bindings body fail)
     (if (stream-pair? stream)
         (stream-match-pattern (stream-cdr stream) rest bindings body fail)
         fail))
    ((_ stream (pattern . rest) bindings body fail)
     (if (stream-pair? stream)
         (stream-match-pattern
          (stream-cdr stream) rest
          ((pattern (stream-car stream)) . bindings)
          body fail)
         fail))
    ((_ stream _ bindings body fail) (let bindings body))
    ((_ stream pattern bindings body fail)
     (let ((pattern stream) . bindings) body))))

(define-syntax stream-of
  (syntax-rules ()
    ((_ expression clause ...)
     (stream-of-aux expression stream-null clause ...))))

(define-syntax stream-of-aux
  (syntax-rules (in is)
    ((_ expression base)
     (stream-cons expression base))
    ((_ expression base (var in stream) clause ...)
     (stream-let loop ((source stream))
       (if (stream-null? source)
           base
           (let ((var (stream-car source)))
             (stream-of-aux expression (loop (stream-cdr source))
                            clause ...)))))
    ((_ expression base (var is value) clause ...)
     (let ((var value))
       (stream-of-aux expression base clause ...)))
    ((_ expression base pred clause ...)
     (if pred
         (stream-of-aux expression base clause ...)
         base))))

(define (stream-range first past . rest)
  (let ((step (cond ((pair? rest) (car rest))
                    ((< first past) 1)
                    (else -1))))
    (let loop ((value first))
      (if (if (positive? step) (< value past) (> value past))
          (stream-cons value (loop (+ value step)))
          stream-null))))

(define (stream-ref stream index)
  (cond ((not (stream? stream)) (error "stream-ref: non-stream" stream))
        ((not (integer? index)) (error "stream-ref: non-integer" index))
        ((negative? index) (error "stream-ref: negative" index))
        (else
         (let loop ((stream stream) (left index))
           (cond ((stream-null? stream)
                  (error "stream-ref: beyond end of stream"))
                 ((zero? left) (stream-car stream))
                 (else (loop (stream-cdr stream) (- left 1))))))))

(define (stream-reverse stream)
  (if (not (stream? stream))
      (error "stream-reverse: non-stream" stream)
      (letrec
          ((reverse-stream
            (stream-lambda (stream result)
              (if (stream-null? stream)
                  result
                  (reverse-stream (stream-cdr stream)
                                  (stream-cons (stream-car stream)
                                               result))))))
        (reverse-stream stream stream-null))))

(define (stream-scan proc base stream)
  (if (not (stream? stream))
      (error "stream-scan: non-stream" stream)
      (letrec
          ((scan-stream
            (stream-lambda (base stream)
              (stream-cons
               base
               (if (stream-null? stream)
                   stream-null
                   (scan-stream (proc base (stream-car stream))
                                (stream-cdr stream)))))))
        (scan-stream base stream))))

(define (stream-take count stream)
  (cond ((not (stream? stream)) (error "stream-take: non-stream" stream))
        ((not (integer? count)) (error "stream-take: non-integer" count))
        ((negative? count) (error "stream-take: negative" count))
        (else
         (letrec
             ((take-stream
               (stream-lambda (left stream)
                 (if (or (stream-null? stream) (zero? left))
                     stream-null
                     (stream-cons (stream-car stream)
                                  (take-stream (- left 1)
                                               (stream-cdr stream)))))))
           (take-stream count stream)))))

(define (stream-take-while pred stream)
  (if (not (stream? stream))
      (error "stream-take-while: non-stream" stream)
      (letrec
          ((take-stream
            (stream-lambda (stream)
              (cond ((stream-null? stream) stream-null)
                    ((pred (stream-car stream))
                     (stream-cons (stream-car stream)
                                  (take-stream (stream-cdr stream))))
                    (else stream-null)))))
        (take-stream stream))))

(define (stream-unfold mapper pred generator base)
  (letrec
      ((unfold-stream
        (stream-lambda (base)
          (if (pred base)
              (stream-cons (mapper base)
                           (unfold-stream (generator base)))
              stream-null))))
    (unfold-stream base)))

(define (stream-unfolds proc seed)
  (define (value-count)
    (call-with-values (lambda () (proc seed))
                      (lambda values (- (length values) 1))))
  (define (result-stream seed)
    (call-with-values
     (lambda () (proc seed))
     (lambda (next . results)
       (stream-cons results (result-stream next)))))
  (define (output-stream results index)
    (let ((result (list-ref (stream-car results) (- index 1))))
      (cond ((pair? result)
             (stream-cons
              (car result)
              (output-stream (stream-cdr results) index)))
            ((not result) (output-stream (stream-cdr results) index))
            ((null? result) stream-null)
            (else (error "stream-unfolds: invalid result" result)))))
  (define (output-streams results)
    (let loop ((index (value-count)) (outputs '()))
      (if (zero? index)
          (apply values outputs)
          (loop (- index 1)
                (cons (output-stream results index) outputs)))))
  (if (not (procedure? proc))
      (error "stream-unfolds: non-procedural argument" proc)
      (output-streams (result-stream seed))))

(define (stream-zip . streams)
  (cond ((null? streams) (error "stream-zip: no streams"))
        ((stream-any (lambda (stream) (not (stream? stream))) streams)
         (error "stream-zip: non-stream argument"))
        (else
         (letrec
             ((zip-streams
               (stream-lambda (streams)
                 (if (stream-any stream-null? streams)
                     stream-null
                     (stream-cons (map stream-car streams)
                                  (zip-streams (map stream-cdr streams)))))))
           (zip-streams streams)))))

(define _ (quote _))
