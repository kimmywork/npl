; SRFI 140 immutable strings with the canonical NPL library name.
;
; An immutable string is a string carrying NPL's literal (frozen) flag. The
; constructors here copy their input and freeze the copy with
; npl_freeze_literal, so (scheme base) string-set! and string-fill! reject
; them. istring? probes the flag through string-fill! on an empty range,
; which is the only mutation question with an observable answer and has no
; effect on a mutable string.
;
; SRFI 140 keeps the R7RS names but changes the result type: string-append,
; string-map, substring, and the SRFI 13-style operations return istrings.
; string-copy, make-string, string-set!, string-fill!, and string-copy!
; keep their R7RS mutable behavior.
(define-library (npl string immutable)
  (export
    string? istring? string-null? string-every string-any
    string->vector string->list vector->string list->string
    reverse-list->string
    string->utf8 string->utf16 string->utf16be string->utf16le
    utf8->string utf16->string utf16be->string utf16le->string
    string string-append string-tabulate string-unfold string-unfold-right
    string-length string-ref substring
    string-take string-take-right string-drop string-drop-right
    string-pad string-pad-right
    string-trim string-trim-right string-trim-both
    string-replace string-copy string-copy! string-fill! string-set!
    string-concatenate string-concatenate-reverse string-join
    string-fold string-fold-right string-map string-map-index
    string-for-each string-for-each-index
    string-contains string-contains-right
    string-index string-index-right string-count
    string-filter string-remove string-split string-tokenize
    string-replicate string-repeat xsubstring
    string-upcase string-downcase string-foldcase string-titlecase)
  (import (except (scheme base)
                  string string-append list->string vector->string
                  substring string-map utf8->string)
          (rename (only (scheme base)
                        string-map list->string vector->string string-append
                        substring utf8->string)
                  (string-map %base-string-map)
                  (list->string %base-list->string)
                  (vector->string %base-vector->string)
                  (string-append %base-string-append)
                  (substring %base-substring)
                  (utf8->string %base-utf8->string))
          (rename (only (scheme char) string-foldcase)
                  (string-foldcase %char-string-foldcase))
          (only (npl internal primitive) npl_freeze_literal)
          (prefix (npl string) s13:)
          (prefix (npl string cursor) sc:))
  (begin
    (define (%freeze value) (npl_freeze_literal value))

    (define (%range who s args)
      (let* ((n (string-length s))
             (start (if (pair? args) (car args) 0))
             (rest (if (pair? args) (cdr args) '()))
             (end (if (pair? rest) (car rest) n)))
        (if (and (exact-integer? start) (exact-integer? end)
                 (<= 0 start end n))
            (cons start end)
            (error who "invalid range" start end))))

    (define (istring? value)
      (and (string? value)
           (guard (e (#t #t))
             (string-fill! value #\space 0 0)
             #f)))

    (define (string-null? s) (s13:string-null? s))
    (define (string-every criterion s . rest)
      (apply s13:string-every criterion s rest))
    (define (string-any criterion s . rest)
      (apply s13:string-any criterion s rest))

    (define (string . characters)
      (%freeze (list->string characters)))
    (define (list->string characters)
      (%freeze (%base-list->string characters)))
    (define (vector->string vector . rest)
      (%freeze (apply %base-vector->string vector rest)))
    (define (string-append . strings)
      (%freeze (apply %base-string-append strings)))
    (define (substring s start end)
      (%freeze (%base-substring s start end)))
    (define (string-map proc s . rest)
      (%freeze (apply %base-string-map proc s rest)))
    (define (string-tabulate proc len)
      (%freeze (s13:string-tabulate proc len)))
    (define (string-unfold stop? mapper successor seed . rest)
      (%freeze (apply s13:string-unfold stop? mapper successor seed rest)))
    (define (string-unfold-right stop? mapper successor seed . rest)
      (%freeze (apply s13:string-unfold-right stop? mapper successor seed rest)))
    (define (reverse-list->string characters)
      (%freeze (s13:reverse-list->string characters)))
    (define (string-take s n) (%freeze (s13:string-take s n)))
    (define (string-take-right s n) (%freeze (s13:string-take-right s n)))
    (define (string-drop s n) (%freeze (s13:string-drop s n)))
    (define (string-drop-right s n) (%freeze (s13:string-drop-right s n)))
    (define (string-pad s len . rest)
      (%freeze (apply s13:string-pad s len rest)))
    (define (string-pad-right s len . rest)
      (%freeze (apply s13:string-pad-right s len rest)))
    (define (string-trim s . rest)
      (%freeze (apply s13:string-trim s rest)))
    (define (string-trim-right s . rest)
      (%freeze (apply s13:string-trim-right s rest)))
    (define (string-trim-both s . rest)
      (%freeze (apply s13:string-trim-both s rest)))
    (define (string-replace s1 s2 start1 end1 . rest)
      (%freeze (apply s13:string-replace s1 s2 start1 end1 rest)))
    (define (string-concatenate strings)
      (%freeze (s13:string-concatenate strings)))
    (define (string-concatenate-reverse strings . rest)
      (%freeze (apply s13:string-concatenate-reverse strings rest)))
    (define (string-join strings . rest)
      (%freeze (apply s13:string-join strings rest)))
    (define (string-fold kons knil s . rest)
      (apply s13:string-fold kons knil s rest))
    (define (string-fold-right kons knil s . rest)
      (apply s13:string-fold-right kons knil s rest))
    (define (string-for-each-index proc s . rest)
      (apply s13:string-for-each-index proc s rest))
    (define (string-map-index proc s . rest)
      (let* ((range (%range "string-map-index" s rest))
             (start (car range))
             (result (make-string (- (cdr range) start))))
        (let loop ((i start) (j 0))
          (if (< i (cdr range))
              (begin
                (string-set! result j (proc i))
                (loop (+ i 1) (+ j 1)))))
        (%freeze result)))
    (define (string-contains s1 s2 . rest)
      (apply s13:string-contains s1 s2 rest))
    (define (string-contains-right s1 s2 . rest)
      (apply sc:string-contains-right s1 s2 rest))
    (define (string-index s criterion . rest)
      (apply s13:string-index s criterion rest))
    (define (string-index-right s criterion . rest)
      (apply s13:string-index-right s criterion rest))
    (define (string-count s criterion . rest)
      (apply s13:string-count s criterion rest))
    (define (string-filter criterion s . rest)
      (%freeze (apply s13:string-filter criterion s rest)))
    (define (string-remove criterion s . rest)
      (%freeze (apply s13:string-delete criterion s rest)))
    (define (string-split s delimiter . rest)
      (%freeze-list (apply sc:string-split s delimiter rest)))
    (define (string-tokenize s . rest)
      (%freeze-list (apply s13:string-tokenize s rest)))
    (define (string-replicate s from to . rest)
      (%freeze (apply sc:string-replicate s from to rest)))
    (define (string-repeat s count)
      (%freeze (s13:xsubstring s 0 (* count (string-length s)))))
    (define (xsubstring s from . rest)
      (%freeze (apply s13:xsubstring s from rest)))
    (define (string-upcase s . rest)
      (%freeze (apply s13:string-upcase s rest)))
    (define (string-downcase s . rest)
      (%freeze (apply s13:string-downcase s rest)))
    (define (string-titlecase s . rest)
      (%freeze (apply s13:string-titlecase s rest)))
    (define (string-foldcase s)
      (%freeze (%char-string-foldcase s)))

    (define (%freeze-list strings)
      (map %freeze strings))

    (define (%u16-ref bytes index big?)
      (if big?
          (+ (* 256 (bytevector-u8-ref bytes index))
             (bytevector-u8-ref bytes (+ index 1)))
          (+ (bytevector-u8-ref bytes index)
             (* 256 (bytevector-u8-ref bytes (+ index 1))))))

    (define (%u16-set! bytes index code big?)
      (let ((high (quotient code 256))
            (low (modulo code 256)))
        (bytevector-u8-set! bytes index (if big? high low))
        (bytevector-u8-set! bytes (+ index 1) (if big? low high))))

    (define (%utf16-encode s start end big?)
      (let* ((count (- end start))
             (bytes (make-bytevector (* 2 count) 0)))
        (let loop ((i 0) (at 0))
          (if (< i count)
              (let ((code (char->integer (string-ref s (+ start i)))))
                (if (<= code #xFFFF)
                    (begin
                      (%u16-set! bytes at code big?)
                      (loop (+ i 1) (+ at 2)))
                    (let* ((value (- code #x10000))
                           (high (+ #xD800 (quotient value 1024)))
                           (low (+ #xDC00 (modulo value 1024))))
                      (%u16-set! bytes at high big?)
                      (%u16-set! bytes (+ at 2) low big?)
                      (loop (+ i 1) (+ at 4)))))
              bytes))))

    (define (%parse-utf16-range who bytes rest)
      (let* ((len (bytevector-length bytes))
             (start (if (pair? rest) (car rest) 0))
             (rest (if (pair? rest) (cdr rest) '()))
             (end (if (and (pair? rest) (exact-integer? (car rest)))
                      (car rest) len)))
        (if (and (exact-integer? start) (exact-integer? end)
                 (<= 0 start end len) (even? (- end start)))
            (cons start end)
            (error who "invalid UTF-16 range" start end))))

    (define (%utf16-decode who bytes start end big?)
      (let ((chars '()))
        (let loop ((i start))
          (if (>= i end)
              (list->string (reverse chars))
              (let ((unit (%u16-ref bytes i big?)))
                (cond ((and (>= unit #xD800) (<= unit #xDBFF))
                       (if (>= (+ i 2) end)
                           (error who "truncated surrogate pair")
                           (let ((low (%u16-ref bytes (+ i 2) big?)))
                             (if (and (>= low #xDC00) (<= low #xDFFF))
                                 (begin
                                   (set! chars
                                         (cons (integer->char
                                                (+ #x10000
                                                   (* (- unit #xD800) 1024)
                                                   (- low #xDC00)))
                                               chars))
                                   (loop (+ i 4)))
                                 (error who "invalid surrogate pair")))))
                      ((and (>= unit #xDC00) (<= unit #xDFFF))
                       (error who "unexpected low surrogate"))
                      (else
                       (set! chars (cons (integer->char unit) chars))
                       (loop (+ i 2)))))))))

    (define (string->utf16 s . rest)
      (let* ((range (%range "string->utf16" s rest))
             (body (%utf16-encode s (car range) (cdr range) #t))
             (result (make-bytevector (+ 2 (bytevector-length body)) 0)))
        (%u16-set! result 0 #xFEFF #t)
        (bytevector-copy! result 2 body)
        result))
    (define (string->utf16be s . rest)
      (let ((range (%range "string->utf16be" s rest)))
        (%utf16-encode s (car range) (cdr range) #t)))
    (define (string->utf16le s . rest)
      (let ((range (%range "string->utf16le" s rest)))
        (%utf16-encode s (car range) (cdr range) #f)))

    (define (utf8->string bytes . rest)
      (%freeze (apply %base-utf8->string bytes rest)))
    (define (utf16be->string bytes . rest)
      (let ((range (%parse-utf16-range "utf16be->string" bytes rest)))
        (%freeze (%utf16-decode "utf16be->string" bytes
                                (car range) (cdr range) #t))))
    (define (utf16le->string bytes . rest)
      (let ((range (%parse-utf16-range "utf16le->string" bytes rest)))
        (%freeze (%utf16-decode "utf16le->string" bytes
                                (car range) (cdr range) #f))))
    (define (utf16->string bytes . rest)
      (let* ((range (%parse-utf16-range "utf16->string" bytes rest))
             (start (car range))
             (end (cdr range))
             (bom? (and (>= (- end start) 2)
                        (= (%u16-ref bytes start #t) #xFEFF))))
        (if bom?
            (%freeze (%utf16-decode "utf16->string" bytes
                                    (+ start 2) end #t))
            (%freeze (%utf16-decode "utf16->string" bytes
                                    start end #t))))))
)
