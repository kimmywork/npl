; SRFI 13 searching, prefix/suffix, concatenation, replacement, replication,
; hash, and tokenization. See string.sld.

;;;; Prefixes and suffixes

(define (%string-prefix-length who s1 s2 args fold)
  (let* ((range (%string-ranges who s1 s2 args))
         (end1 (cadr range))
         (end2 (cadddr range)))
    (let loop ((i (car range)) (j (caddr range)) (n 0))
      (if (and (< i end1) (< j end2)
               (char=? (fold (string-ref s1 i))
                       (fold (string-ref s2 j))))
          (loop (+ i 1) (+ j 1) (+ n 1))
          n))))

(define (string-prefix-length s1 s2 . rest)
  (%string-prefix-length "string-prefix-length" s1 s2 rest %string-identity))

(define (string-prefix-length-ci s1 s2 . rest)
  (%string-prefix-length "string-prefix-length-ci" s1 s2 rest char-foldcase))

(define (string-prefix? s1 s2 . rest)
  (let* ((range (%string-ranges "string-prefix?" s1 s2 rest))
         (len1 (- (cadr range) (car range)))
         (len2 (- (cadddr range) (caddr range))))
    (and (<= len1 len2)
         (= len1 (%string-prefix-length "string-prefix?" s1 s2 rest
                                        %string-identity)))))

(define (string-prefix-ci? s1 s2 . rest)
  (let* ((range (%string-ranges "string-prefix-ci?" s1 s2 rest))
         (len1 (- (cadr range) (car range)))
         (len2 (- (cadddr range) (caddr range))))
    (and (<= len1 len2)
         (= len1 (%string-prefix-length "string-prefix-ci?" s1 s2 rest
                                        char-foldcase)))))

(define (%string-suffix-length who s1 s2 args fold)
  (let* ((range (%string-ranges who s1 s2 args))
         (start1 (car range))
         (start2 (caddr range)))
    (let loop ((i (- (cadr range) 1))
               (j (- (cadddr range) 1))
               (n 0))
      (if (and (>= i start1) (>= j start2)
               (char=? (fold (string-ref s1 i))
                       (fold (string-ref s2 j))))
          (loop (- i 1) (- j 1) (+ n 1))
          n))))

(define (string-suffix-length s1 s2 . rest)
  (%string-suffix-length "string-suffix-length" s1 s2 rest %string-identity))

(define (string-suffix-length-ci s1 s2 . rest)
  (%string-suffix-length "string-suffix-length-ci" s1 s2 rest char-foldcase))

(define (string-suffix? s1 s2 . rest)
  (let* ((range (%string-ranges "string-suffix?" s1 s2 rest))
         (len1 (- (cadr range) (car range)))
         (len2 (- (cadddr range) (caddr range))))
    (and (<= len1 len2)
         (= len1 (%string-suffix-length "string-suffix?" s1 s2 rest
                                        %string-identity)))))

(define (string-suffix-ci? s1 s2 . rest)
  (let* ((range (%string-ranges "string-suffix-ci?" s1 s2 rest))
         (len1 (- (cadr range) (car range)))
         (len2 (- (cadddr range) (caddr range))))
    (and (<= len1 len2)
         (= len1 (%string-suffix-length "string-suffix-ci?" s1 s2 rest
                                        char-foldcase)))))

;;;; Searching

(define (string-index s criterion . rest)
  (let* ((range (%string-range "string-index" s rest))
         (pred (%string-criterion "string-index" criterion))
         (end (cdr range)))
    (let loop ((i (car range)))
      (cond ((>= i end) #f)
            ((pred (string-ref s i)) i)
            (else (loop (+ i 1)))))))

(define (string-index-right s criterion . rest)
  (let* ((range (%string-range "string-index-right" s rest))
         (pred (%string-criterion "string-index-right" criterion))
         (start (car range)))
    (let loop ((i (- (cdr range) 1)))
      (cond ((< i start) #f)
            ((pred (string-ref s i)) i)
            (else (loop (- i 1)))))))

(define (string-skip s criterion . rest)
  (let* ((range (%string-range "string-skip" s rest))
         (pred (%string-criterion "string-skip" criterion))
         (end (cdr range)))
    (let loop ((i (car range)))
      (cond ((>= i end) #f)
            ((pred (string-ref s i)) (loop (+ i 1)))
            (else i)))))

(define (string-skip-right s criterion . rest)
  (let* ((range (%string-range "string-skip-right" s rest))
         (pred (%string-criterion "string-skip-right" criterion))
         (start (car range)))
    (let loop ((i (- (cdr range) 1)))
      (cond ((< i start) #f)
            ((pred (string-ref s i)) (loop (- i 1)))
            (else i)))))

(define (string-count s criterion . rest)
  (let* ((range (%string-range "string-count" s rest))
         (pred (%string-criterion "string-count" criterion))
         (end (cdr range)))
    (let loop ((i (car range)) (n 0))
      (if (>= i end)
          n
          (loop (+ i 1) (if (pred (string-ref s i)) (+ n 1) n))))))

(define (%string-contains who s1 s2 args fold)
  (let* ((range (%string-ranges who s1 s2 args))
         (start1 (car range))
         (end1 (cadr range))
         (start2 (caddr range))
         (end2 (cadddr range))
         (len2 (- end2 start2)))
    (if (zero? len2)
        start1
        (let loop ((i start1))
          (if (> (+ i len2) end1)
              #f
              (let match ((k 0))
                (cond ((>= k len2) i)
                      ((char=? (fold (string-ref s1 (+ i k)))
                               (fold (string-ref s2 (+ start2 k))))
                       (match (+ k 1)))
                      (else (loop (+ i 1))))))))))

(define (string-contains s1 s2 . rest)
  (%string-contains "string-contains" s1 s2 rest %string-identity))

(define (string-contains-ci s1 s2 . rest)
  (%string-contains "string-contains-ci" s1 s2 rest char-foldcase))

;;;; Filtering and deletion

(define (string-filter criterion s . rest)
  (let* ((range (%string-range "string-filter" s rest))
         (pred (%string-criterion "string-filter" criterion))
         (start (car range))
         (end (cdr range)))
    (let loop ((i start) (chars '()))
      (if (>= i end)
          (list->string (reverse chars))
          (let ((c (string-ref s i)))
            (loop (+ i 1) (if (pred c) (cons c chars) chars)))))))

(define (string-delete criterion s . rest)
  (let* ((range (%string-range "string-delete" s rest))
         (pred (%string-criterion "string-delete" criterion))
         (start (car range))
         (end (cdr range)))
    (let loop ((i start) (chars '()))
      (if (>= i end)
          (list->string (reverse chars))
          (let ((c (string-ref s i)))
            (loop (+ i 1) (if (pred c) chars (cons c chars))))))))

;;;; Reversal

(define (string-reverse s . rest)
  (let* ((range (%string-range "string-reverse" s rest))
         (start (car range))
         (end (cdr range))
         (result (make-string (- end start))))
    (let loop ((i start) (j (- (string-length result) 1)))
      (if (< i end)
          (begin
            (string-set! result j (string-ref s i))
            (loop (+ i 1) (- j 1)))))
    result))

(define (string-reverse! s . rest)
  (let* ((range (%string-range "string-reverse!" s rest))
         (start (car range))
         (end (cdr range)))
    (let loop ((i start) (j (- end 1)))
      (if (< i j)
          (let ((left (string-ref s i))
                (right (string-ref s j)))
            (string-set! s i right)
            (string-set! s j left)
            (loop (+ i 1) (- j 1)))))
    s))

;;;; Concatenation

(define (string-concatenate strings)
  (let ((total (let loop ((items strings) (n 0))
                 (if (pair? items)
                     (loop (cdr items) (+ n (string-length (car items))))
                     n))))
    (let ((result (make-string total)))
      (let loop ((items strings) (at 0))
        (if (pair? items)
            (let* ((s (car items))
                   (len (string-length s)))
              (string-copy-into result at s 0 len)
              (loop (cdr items) (+ at len)))))
      result)))

(define (string-concatenate/shared strings)
  (string-concatenate strings))

(define (string-append/shared . strings)
  (string-concatenate strings))

(define (string-concatenate-reverse strings . rest)
  (let* ((final (if (pair? rest) (car rest) ""))
         (range (%string-range "string-concatenate-reverse" final
                               (if (and (pair? rest) (pair? (cdr rest)))
                                   (list (car (cdr rest)))
                                   '())))
         (tail (substring final (car range) (cdr range))))
    (string-concatenate (append (reverse strings) (list tail)))))

(define (string-concatenate-reverse/shared strings . rest)
  (apply string-concatenate-reverse strings rest))

;;;; Replacement and replication

(define (string-replace s1 s2 start1 end1 . rest)
  (let* ((range1 (%string-range "string-replace" s1 (list start1 end1)))
         (range2 (%string-range "string-replace" s2 rest))
         (start2 (car range2))
         (end2 (cdr range2))
         (prefix (substring s1 0 start1))
         (middle (substring s2 start2 end2))
         (suffix (substring s1 end1 (string-length s1))))
    (string-append prefix middle suffix)))

(define (xsubstring s from . rest)
  (let* ((to (if (pair? rest) (car rest) (+ from (string-length s))))
         (range (%string-range "xsubstring" s
                               (if (pair? rest) (cdr rest) '())))
         (start (car range))
         (end (cdr range))
         (slen (- end start))
         (anslen (- to from)))
    (cond ((or (not (exact-integer? from)) (not (exact-integer? to))
               (> from to))
           (%string-error "xsubstring" "invalid range" from to))
          ((zero? anslen) "")
          ((zero? slen)
           (%string-error "xsubstring" "cannot replicate an empty string"))
          (else
           (let ((result (make-string anslen)))
             (let loop ((k 0))
               (if (< k anslen)
                   (begin
                     (string-set! result k
                                  (string-ref s (+ start
                                                   (modulo (+ from k) slen))))
                     (loop (+ k 1)))))
             result)))))

(define (string-xcopy! target tstart s sfrom . rest)
  (let* ((sto (if (pair? rest) (car rest) (+ sfrom (string-length s))))
         (range (%string-range "string-xcopy!" s
                               (if (pair? rest) (cdr rest) '())))
         (start (car range))
         (end (cdr range))
         (slen (- end start))
         (count (- sto sfrom)))
    (cond ((or (not (exact-integer? sfrom)) (not (exact-integer? sto))
               (> sfrom sto))
           (%string-error "string-xcopy!" "invalid range" sfrom sto))
          ((zero? count) target)
          ((zero? slen)
           (%string-error "string-xcopy!" "cannot replicate an empty string"))
          (else
           (let loop ((k 0))
             (if (< k count)
                 (begin
                   (string-set! target (+ tstart k)
                                (string-ref s (+ start
                                                 (modulo (+ sfrom k) slen))))
                   (loop (+ k 1)))))
           target))))

;;;; Hash and tokenization

(define (string-hash-range s args fold bound)
  (let* ((range (%string-range "string-hash" s args))
         (end (cdr range)))
    (let loop ((i (car range)) (h 5381))
      (if (>= i end)
          (modulo h bound)
          (loop (+ i 1)
                (modulo (+ (* h 33) (char->integer (fold (string-ref s i))))
                        2147483648))))))

(define (string-hash s . rest)
  (let ((bound (if (pair? rest) (car rest) 4194304)))
    (if (and (exact-integer? bound) (positive? bound))
        (string-hash-range s (if (pair? rest) (cdr rest) '())
                           %string-identity bound)
        (%string-error "string-hash" "invalid bound" bound))))

(define (string-hash-ci s . rest)
  (let ((bound (if (pair? rest) (car rest) 4194304)))
    (if (and (exact-integer? bound) (positive? bound))
        (string-hash-range s (if (pair? rest) (cdr rest) '())
                           char-foldcase bound)
        (%string-error "string-hash-ci" "invalid bound" bound))))

(define (%graphic-char? c)
  (let ((code (char->integer c)))
    (and (>= code 32)
         (not (= code 127))
         (not (char-whitespace? c)))))

(define (string-tokenize s . rest)
  (let* ((criterion (if (pair? rest) (car rest) %graphic-char?))
         (range (%string-range "string-tokenize" s
                               (if (pair? rest) (cdr rest) '())))
         (pred (%string-criterion "string-tokenize" criterion))
         (start (car range))
         (end (cdr range)))
    (let loop ((i start) (tokens '()))
      (cond ((>= i end) (reverse tokens))
            ((pred (string-ref s i))
             (let scan ((j (+ i 1)))
               (if (and (< j end) (pred (string-ref s j)))
                   (scan (+ j 1))
                   (loop j (cons (substring s i j) tokens)))))
            (else (loop (+ i 1) tokens))))))
