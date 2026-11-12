; SRFI 41 streams with the canonical NPL library name.
;
; A stream is a promise of either an empty list or a pair of an element and
; the rest of the stream. stream-null is the promise of the empty stream;
; accessors force their argument. This is the SRFI 41 reference model.
(define-library (npl data stream)
  (export
    stream-null stream-cons stream? stream-null? stream-pair?
    stream-car stream-cdr stream-lambda define-stream
    list->stream port->stream stream stream->list
    stream-append stream-concat stream-constant
    stream-drop stream-drop-while stream-filter stream-fold
    stream-for-each stream-from stream-iterate stream-length
    stream-let stream-map stream-match _
    stream-of stream-range stream-ref stream-reverse stream-scan
    stream-take stream-take-while stream-unfold stream-unfolds stream-zip)
  (import (scheme base)
          (scheme lazy))
  (begin
    (define stream-null (delay '()))

    (define-syntax stream-cons
      (syntax-rules ()
        ((_ object rest) (delay (cons object rest)))))

    (define (stream-null? stream) (null? (force stream)))
    (define (stream-pair? stream) (pair? (force stream)))
    (define (stream-car stream) (car (force stream)))
    (define (stream-cdr stream) (cdr (force stream)))
    (define (stream? stream) (or (stream-null? stream) (stream-pair? stream)))

    (define-syntax stream-lambda
      (syntax-rules ()
        ((_ formals body ...)
         (lambda formals (delay (let () body ...))))))

    (define-syntax define-stream
      (syntax-rules ()
        ((_ (name . formals) body ...)
         (define name (stream-lambda formals body ...)))))

    (define-syntax stream
      (syntax-rules ()
        ((_) stream-null)
        ((_ x y ...) (stream-cons x (stream y ...)))))

    (define (list->stream items)
      (if (null? items)
          stream-null
          (stream-cons (car items) (list->stream (cdr items)))))

    (define (port->stream . rest)
      (let ((port (if (pair? rest) (car rest) (current-input-port))))
        (let loop ()
          (let ((c (read-char port)))
            (if (eof-object? c)
                stream-null
                (stream-cons c (loop)))))))

    (define (stream->list stream . rest)
      (let ((count (if (pair? rest) (car rest) #f)))
        (if (and count (exact-integer? stream) (not (exact-integer? count)))
            (stream->list count stream)
            (let loop ((stream stream) (count count) (items '()))
              (if (and count (<= count 0))
                  (reverse items)
                  (if (stream-null? stream)
                      (reverse items)
                      (loop (stream-cdr stream)
                            (if count (- count 1) #f)
                            (cons (stream-car stream) items))))))))

    (define (stream-append . streams)
      (if (null? streams)
          stream-null
          (let ((stream (car streams)))
            (if (stream-null? stream)
                (apply stream-append (cdr streams))
                (stream-cons (stream-car stream)
                             (apply stream-append
                                    (stream-cdr stream) (cdr streams)))))))

    (define (stream-concat streams)
      (if (stream-null? streams)
          stream-null
          (let ((first (stream-car streams)))
            (if (stream-null? first)
                (stream-concat (stream-cdr streams))
                (stream-cons (stream-car first)
                             (stream-concat
                              (stream-cons (stream-cdr first)
                                           (stream-cdr streams))))))))

    (define (stream-constant . objects)
      (let loop ((items objects))
        (if (null? items)
            (loop objects)
            (stream-cons (car items) (loop (cdr items))))))

    (define (stream-drop stream count)
      (let loop ((stream stream) (count count))
        (if (or (<= count 0) (stream-null? stream))
            stream
            (loop (stream-cdr stream) (- count 1)))))

    (define (stream-drop-while pred stream)
      (let loop ((stream stream))
        (if (and (not (stream-null? stream)) (pred (stream-car stream)))
            (loop (stream-cdr stream))
            stream)))

    (define (stream-filter pred stream)
      (let loop ((stream stream))
        (cond ((stream-null? stream) stream-null)
              ((pred (stream-car stream))
               (stream-cons (stream-car stream) (loop (stream-cdr stream))))
              (else (loop (stream-cdr stream))))))

    (define (stream-fold proc base stream)
      (let loop ((stream stream) (state base))
        (if (stream-null? stream)
            state
            (loop (stream-cdr stream) (proc state (stream-car stream))))))

    (define (stream-for-each proc . streams)
      (if (null? streams)
          (if #f #f)
          (let ((stream (car streams)))
            (if (stream-null? stream)
                (if #f #f)
                (begin
                  (apply proc (map stream-car streams))
                  (apply stream-for-each proc (map stream-cdr streams)))))))

    (define (stream-from first . rest)
      (let ((step (if (pair? rest) (car rest) 1)))
        (let loop ((value first))
          (stream-cons value (loop (+ value step))))))

    (define (stream-iterate proc base)
      (stream-cons base (stream-iterate proc (proc base))))

    (define (stream-length stream)
      (let loop ((stream stream) (n 0))
        (if (stream-null? stream)
            n
            (loop (stream-cdr stream) (+ n 1)))))

    (define-syntax stream-let
      (syntax-rules ()
        ((_ tag ((name value) ...) body ...)
         ((letrec ((tag (stream-lambda (name ...) body ...))) tag)
          value ...))))

    (define (stream-map proc . streams)
      (if (or (null? streams) (stream-null? (car streams)))
          stream-null
          (stream-cons
           (apply proc (map stream-car streams))
           (apply stream-map proc (map stream-cdr streams)))))

    (define _ '_)

    (define-syntax stream-match
      (syntax-rules (_)
        ((_ stream-expr) (error "stream-match: no matching clause"))
        ((_ stream-expr (_ body ...) clause ...)
         (begin body ...))
        ((_ stream-expr (() body ...) clause ...)
         (if (stream-null? stream-expr)
             (begin body ...)
             (stream-match stream-expr clause ...)))
        ((_ stream-expr ((pattern . rest) body ...) clause ...)
         (if (stream-pair? stream-expr)
             (let ((pattern (stream-car stream-expr))
                   (rest (stream-cdr stream-expr)))
               (begin body ...))
             (stream-match stream-expr clause ...)))
        ((_ stream-expr (pattern body ...) clause ...)
         (let ((pattern stream-expr)) (begin body ...)))))

    (define-syntax stream-of
      (syntax-rules (in if)
        ((_ expr) (stream-cons expr stream-null))
        ((_ expr (var in stream) if pred)
         (stream-concat
          (stream-map (lambda (var) (stream-of expr))
                      (stream-filter (lambda (var) pred) stream))))
        ((_ expr (var in stream) rest ...)
         (stream-concat
          (stream-map (lambda (var) (stream-of expr rest ...)) stream)))))

    (define (stream-range first past . rest)
      (let ((step (if (pair? rest) (car rest) 1)))
        (let loop ((value first))
          (if (if (positive? step) (>= value past) (<= value past))
              stream-null
              (stream-cons value (loop (+ value step)))))))

    (define (stream-ref stream index)
      (let loop ((stream stream) (index index))
        (if (<= index 0)
            (stream-car stream)
            (loop (stream-cdr stream) (- index 1)))))

    (define (stream-reverse stream)
      (let loop ((stream stream) (items '()))
        (if (stream-null? stream)
            (list->stream items)
            (loop (stream-cdr stream) (cons (stream-car stream) items)))))

    (define (stream-scan proc base stream)
      (stream-cons base
                   (if (stream-null? stream)
                       stream-null
                       (stream-scan proc
                                    (proc base (stream-car stream))
                                    (stream-cdr stream)))))

    (define (stream-take stream count)
      (if (or (<= count 0) (stream-null? stream))
          stream-null
          (stream-cons (stream-car stream)
                       (stream-take (stream-cdr stream) (- count 1)))))

    (define (stream-take-while pred stream)
      (if (or (stream-null? stream) (not (pred (stream-car stream))))
          stream-null
          (stream-cons (stream-car stream)
                       (stream-take-while pred (stream-cdr stream)))))

    (define (stream-unfold mapper pred? generator base)
      (if (pred? base)
          (stream-cons (mapper base)
                       (stream-unfold mapper pred? generator (generator base)))
          stream-null))

    (define (stream-zip . streams)
      (if (or (null? streams) (stream-null? (car streams)))
          stream-null
          (stream-cons (map stream-car streams)
                       (apply stream-zip (map stream-cdr streams)))))

    (define (stream-unfolds proc seed)
      (let* ((first (call-with-values (lambda () (proc seed)) list))
             (count (length (cdr first)))
             (state (vector (car first) (make-vector count '())
                            (make-vector count #f))))
        (define (distribute items)
          (let loop ((i 0) (items items))
            (if (pair? items)
                (let ((item (car items)))
                  (cond ((null? item)
                         (vector-set! (vector-ref state 2) i #t))
                        ((eq? item #f) (if #f #f))
                        (else
                         (vector-set! (vector-ref state 1) i
                                      (append (vector-ref (vector-ref state 1) i)
                                              item))))
                  (loop (+ i 1) (cdr items))))))
        (define (advance!)
          (let ((results (call-with-values
                          (lambda () (proc (vector-ref state 0))) list)))
            (vector-set! state 0 (car results))
            (distribute (cdr results))))
        (define (make-stream i)
          (delay
            (let loop ()
              (let ((pending (vector-ref (vector-ref state 1) i)))
                (cond ((pair? pending)
                       (vector-set! (vector-ref state 1) i (cdr pending))
                       (cons (car pending) (make-stream i)))
                      ((vector-ref (vector-ref state 2) i) '())
                      (else (advance!) (loop)))))))
        (distribute (cdr first))
        (let loop ((i 0) (streams '()))
          (if (< i count)
              (loop (+ i 1) (cons (make-stream i) streams))
              (apply values streams)))))

))
