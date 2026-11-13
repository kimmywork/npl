; SRFI 130 string cursors with the canonical NPL library name.
;
; A cursor is an opaque record holding a character index. Procedures that
; take start/end arguments accept either a cursor or an exact integer, as
; SRFI 130 requires; cursors returned by this library are always records.
(define-library (npl string cursor)
  (export
    string-cursor? string-cursor-start string-cursor-end
    string-cursor-next string-cursor-prev
    string-cursor-forward string-cursor-back
    string-cursor=? string-cursor<? string-cursor>?
    string-cursor<=? string-cursor>=?
    string-cursor-diff string-cursor->index string-index->cursor

    string-null? string-every string-any
    string-tabulate string-unfold string-unfold-right
    string->list/cursors string->vector/cursors
    reverse-list->string string-join
    string-ref/cursor substring/cursors string-copy/cursors
    string-take string-take-right string-drop string-drop-right
    string-pad string-pad-right
    string-trim string-trim-right string-trim-both
    string-prefix-length string-suffix-length
    string-prefix? string-suffix?
    string-index string-index-right string-skip string-skip-right
    string-contains string-contains-right
    string-reverse string-concatenate string-concatenate-reverse
    string-fold string-fold-right string-for-each-cursor
    string-replicate string-count string-replace string-split
    string-filter string-remove)
  (import (scheme base) (scheme cxr)
          (prefix (npl string) s13:))
  (include "kernel.scm")
  (begin
    (define-record-type cursor-type
      (make-cursor index)
      string-cursor?
      (index cursor-index))

    (define (%cursor-index who value)
      (if (string-cursor? value)
          (cursor-index value)
          (if (exact-integer? value)
              value
              (error who "expected a cursor or index" value))))

    (define (%normalize args)
      (map (lambda (value)
             (if (string-cursor? value) (cursor-index value) value))
           args))

    (define (%substring s start end)
      (substring s (%cursor-index "substring/cursors" start)
                 (%cursor-index "substring/cursors" end)))

    (define (string-cursor-start s) (make-cursor 0))
    (define (string-cursor-end s) (make-cursor (string-length s)))
    (define (string-cursor-next s cursor)
      (make-cursor (+ 1 (%cursor-index "string-cursor-next" cursor))))
    (define (string-cursor-prev s cursor)
      (make-cursor (- (%cursor-index "string-cursor-prev" cursor) 1)))
    (define (string-cursor-forward s cursor count)
      (make-cursor (+ (%cursor-index "string-cursor-forward" cursor) count)))
    (define (string-cursor-back s cursor count)
      (make-cursor (- (%cursor-index "string-cursor-back" cursor) count)))

    (define (%cursor-order a b) (- (%cursor-index "string-cursor" a)
                                   (%cursor-index "string-cursor" b)))
    (define (string-cursor=? a b) (= 0 (%cursor-order a b)))
    (define (string-cursor<? a b) (< (%cursor-order a b) 0))
    (define (string-cursor>? a b) (> (%cursor-order a b) 0))
    (define (string-cursor<=? a b) (<= (%cursor-order a b) 0))
    (define (string-cursor>=? a b) (>= (%cursor-order a b) 0))
    (define (string-cursor-diff s start end)
      (- (%cursor-index "string-cursor-diff" end)
         (%cursor-index "string-cursor-diff" start)))
    (define (string-cursor->index s cursor)
      (%cursor-index "string-cursor->index" cursor))
    (define (string-index->cursor s index)
      (make-cursor (%cursor-index "string-index->cursor" index)))

    (define (string-null? s) (s13:string-null? s))
    (define (string-tabulate proc len) (s13:string-tabulate proc len))
    (define (string-unfold stop? mapper successor seed . rest)
      (apply s13:string-unfold stop? mapper successor seed rest))
    (define (string-unfold-right stop? mapper successor seed . rest)
      (apply s13:string-unfold-right stop? mapper successor seed rest))
    (define (reverse-list->string characters)
      (s13:reverse-list->string characters))
    (define (string-join strings . rest) (apply s13:string-join strings rest))
    (define (string-take s n) (s13:string-take s n))
    (define (string-take-right s n) (s13:string-take-right s n))
    (define (string-drop s n) (s13:string-drop s n))
    (define (string-drop-right s n) (s13:string-drop-right s n))
    (define (string-reverse s . rest)
      (apply s13:string-reverse s (%normalize rest)))
    (define (string-concatenate strings) (s13:string-concatenate strings))
    (define (string-concatenate-reverse strings . rest)
      (apply s13:string-concatenate-reverse strings rest))

    (define (string-ref/cursor s cursor)
      (string-ref s (%cursor-index "string-ref/cursor" cursor)))

    (define (substring/cursors s start end) (%substring s start end))
    (define (string-copy/cursors s . rest)
      (apply s13:string-copy s (%normalize rest)))

    (define (string->list/cursors s . rest)
      (apply string->list s (%normalize rest)))
    (define (string->vector/cursors s . rest)
      (apply string->vector s (%normalize rest)))

    (define (string-every criterion s . rest)
      (apply s13:string-every criterion s (%normalize rest)))
    (define (string-any criterion s . rest)
      (apply s13:string-any criterion s (%normalize rest)))
    (define (string-pad s len . rest)
      (apply s13:string-pad s len (%normalize rest)))
    (define (string-pad-right s len . rest)
      (apply s13:string-pad-right s len (%normalize rest)))
    (define (string-trim s . rest)
      (apply s13:string-trim s (%normalize rest)))
    (define (string-trim-right s . rest)
      (apply s13:string-trim-right s (%normalize rest)))
    (define (string-trim-both s . rest)
      (apply s13:string-trim-both s (%normalize rest)))
    (define (string-prefix-length s1 s2 . rest)
      (apply s13:string-prefix-length s1 s2 (%normalize rest)))
    (define (string-suffix-length s1 s2 . rest)
      (apply s13:string-suffix-length s1 s2 (%normalize rest)))
    (define (string-prefix? s1 s2 . rest)
      (apply s13:string-prefix? s1 s2 (%normalize rest)))
    (define (string-suffix? s1 s2 . rest)
      (apply s13:string-suffix? s1 s2 (%normalize rest)))
    (define (%search-cursor who s criterion args right? skip?)
      (let* ((range (%string-ranges who s s (%normalize args)))
             (start (car range))
             (end (cadr range))
             (found (apply (cond ((and right? skip?) s13:string-skip-right)
                                 (right? s13:string-index-right)
                                 (skip? s13:string-skip)
                                 (else s13:string-index))
                           s criterion (%normalize args))))
        (if found
            (make-cursor (if right? (+ found 1) found))
            (make-cursor (if right? start end)))))

    (define (string-index s criterion . rest)
      (%search-cursor "string-index" s criterion rest #f #f))
    (define (string-index-right s criterion . rest)
      (%search-cursor "string-index-right" s criterion rest #t #f))
    (define (string-skip s criterion . rest)
      (%search-cursor "string-skip" s criterion rest #f #t))
    (define (string-skip-right s criterion . rest)
      (%search-cursor "string-skip-right" s criterion rest #t #t))
    (define (string-count s criterion . rest)
      (apply s13:string-count s criterion (%normalize rest)))
    (define (string-contains s1 s2 . rest)
      (let ((found (apply s13:string-contains s1 s2 (%normalize rest))))
        (if found (make-cursor found) #f)))
    (define (string-fold kons knil s . rest)
      (apply s13:string-fold kons knil s (%normalize rest)))
    (define (string-fold-right kons knil s . rest)
      (apply s13:string-fold-right kons knil s (%normalize rest)))
    (define (string-replace s1 s2 start1 end1 . rest)
      (apply s13:string-replace s1 s2
             (%cursor-index "string-replace" start1)
             (%cursor-index "string-replace" end1)
             (%normalize rest)))
    (define (string-filter criterion s . rest)
      (apply s13:string-filter criterion s (%normalize rest)))
    (define (string-remove criterion s . rest)
      (apply s13:string-delete criterion s (%normalize rest)))

    (define (string-contains-right s1 s2 . rest)
      (let* ((range (%string-ranges "string-contains-right" s1 s2
                                    (%normalize rest)))
             (start1 (car range))
             (end1 (cadr range))
             (start2 (caddr range))
             (end2 (cadddr range))
             (len2 (- end2 start2)))
        (if (zero? len2)
            (make-cursor end1)
            (let loop ((i (- end1 len2)))
              (cond ((< i start1) #f)
                    ((let match ((k 0))
                       (or (>= k len2)
                           (and (char=? (string-ref s1 (+ i k))
                                        (string-ref s2 (+ start2 k)))
                                (match (+ k 1)))))
                     (make-cursor i))
                    (else (loop (- i 1))))))))

    (define (string-for-each-cursor proc s . rest)
      (let* ((range (%string-ranges "string-for-each-cursor" s s
                                             (%normalize rest)))
             (start (car range))
             (end (cadr range)))
        (let loop ((i start))
          (if (< i end)
              (begin
                (proc (make-cursor i))
                (loop (+ i 1)))))))

    (define (string-replicate s from to . rest)
      (apply s13:xsubstring s from to (%normalize rest)))

    (define (%split-fields s delimiter start end limit)
      (cond ((and (string? delimiter) (zero? (string-length delimiter)))
             (let loop ((i start) (fields '()) (splits 0))
               (cond ((>= i end) (reverse fields))
                     ((and limit (>= splits limit))
                      (reverse (cons (substring s i end) fields)))
                     (else
                      (loop (+ i 1)
                            (cons (substring s i (+ i 1)) fields)
                            (+ splits 1))))))
            (else
             (let* ((dlen (if (char? delimiter) 1 (string-length delimiter)))
                    (matches?
                     (lambda (i)
                       (if (char? delimiter)
                           (char=? delimiter (string-ref s i))
                           (s13:string= delimiter
                                        (substring s i (+ i dlen)))))))
               (let loop ((i start) (mark start) (fields '()) (splits 0))
                 (cond ((>= i end)
                        (reverse (cons (substring s mark end) fields)))
                       ((and limit (>= splits limit))
                        (reverse (cons (substring s mark end) fields)))
                       ((and (<= (+ i dlen) end) (matches? i))
                        (loop (+ i dlen) (+ i dlen)
                              (cons (substring s mark i) fields)
                              (+ splits 1)))
                       (else (loop (+ i 1) mark fields splits))))))))

    (define (string-split s delimiter . rest)
      (let* ((grammar (if (and (pair? rest) (symbol? (car rest)))
                          (car rest)
                          'infix))
             (rest (if (and (pair? rest) (symbol? (car rest)))
                       (cdr rest)
                       rest))
             (limit (if (pair? rest) (car rest) #f))
             (rest (if (pair? rest) (cdr rest) '()))
             (range (%string-range "string-split" s rest))
             (start (car range))
             (end (cdr range)))
        (if (not (or (not limit)
                     (and (exact-integer? limit) (<= 0 limit))))
            (error "string-split: invalid limit" limit))
        (let ((fields (%split-fields s delimiter start end limit)))
          (cond ((eq? grammar 'infix)
                 (if (= start end) '() fields))
                ((eq? grammar 'strict-infix)
                 (if (= start end)
                     (error "string-split: empty input with strict-infix")
                     fields))
                ((eq? grammar 'prefix)
                 (if (and (pair? fields) (string=? (car fields) ""))
                     (cdr fields)
                     fields))
                ((eq? grammar 'suffix)
                 (if (and (pair? fields)
                          (string=? (car (reverse fields)) ""))
                     (reverse (cdr (reverse fields)))
                     fields))
                (else
                 (error "string-split: invalid grammar" grammar)))))))
)
