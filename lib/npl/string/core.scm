; SRFI 13 core: predicates, constructors, conversion, mapping, case,
; selection, trimming, and comparison. See string.sld.

;;;; Predicates

(define (string-null? s)
  (= 0 (string-length s)))

(define (string-every criterion s . rest)
  (let* ((range (%string-range "string-every" s rest))
         (pred (%string-criterion "string-every" criterion))
         (end (cdr range)))
    (let loop ((i (car range)) (result #t))
      (if (>= i end)
          result
          (let ((value (pred (string-ref s i))))
            (if value (loop (+ i 1) value) #f))))))

(define (string-any criterion s . rest)
  (let* ((range (%string-range "string-any" s rest))
         (pred (%string-criterion "string-any" criterion))
         (end (cdr range)))
    (let loop ((i (car range)))
      (if (>= i end)
          #f
          (let ((value (pred (string-ref s i))))
            (if value value (loop (+ i 1))))))))

;;;; Constructors

(define (string-tabulate proc len)
  (if (and (exact-integer? len) (<= 0 len))
      (let ((result (make-string len)))
        (let loop ((i 0))
          (if (< i len)
              (begin
                (string-set! result i (proc i))
                (loop (+ i 1)))))
        result)
      (%string-error "string-tabulate" "invalid length" len)))

(define (string-unfold stop? mapper successor seed . rest)
  (let ((base (%string-part "string-unfold"
                            (if (pair? rest) (car rest) "")))
        (make-final (if (and (pair? rest) (pair? (cdr rest)))
                        (car (cdr rest))
                        (lambda (seed) ""))))
    (let loop ((seed seed) (pieces '()))
      (if (stop? seed)
          (string-concatenate
           (cons base
                 (append (reverse pieces)
                         (list (%string-part "string-unfold"
                                             (make-final seed))))))
          (loop (successor seed)
                (cons (%string-part "string-unfold" (mapper seed))
                      pieces))))))

(define (string-unfold-right stop? mapper successor seed . rest)
  (let ((base (%string-part "string-unfold-right"
                            (if (pair? rest) (car rest) "")))
        (make-final (if (and (pair? rest) (pair? (cdr rest)))
                        (car (cdr rest))
                        (lambda (seed) ""))))
    (let loop ((seed seed) (pieces '()))
      (if (stop? seed)
          (string-concatenate
           (cons (%string-part "string-unfold-right" (make-final seed))
                 (append pieces (list base))))
          (loop (successor seed)
                (cons (%string-part "string-unfold-right" (mapper seed))
                      pieces))))))

(define (reverse-list->string characters)
  (list->string (reverse characters)))

(define (string-join strings . rest)
  (let ((delim (if (pair? rest) (car rest) " "))
        (grammar (if (and (pair? rest) (pair? (cdr rest)))
                     (car (cdr rest))
                     'infix)))
    (let ((build
           (lambda (items final)
             (let recur ((items items))
               (if (pair? items)
                   (cons delim (cons (car items) (recur (cdr items))))
                   final)))))
      (cond ((pair? strings)
             (string-concatenate
              (cond ((memq grammar '(infix strict-infix))
                     (cons (car strings) (build (cdr strings) '())))
                    ((eq? grammar 'prefix) (build strings '()))
                    ((eq? grammar 'suffix)
                     (cons (car strings) (build (cdr strings) (list delim))))
                    (else
                     (%string-error "string-join" "invalid grammar" grammar)))))
            ((null? strings)
             (if (eq? grammar 'strict-infix)
                 (%string-error "string-join"
                                "cannot join an empty list with strict-infix")
                 ""))
            (else
             (%string-error "string-join" "expected a list" strings))))))

;;;; Selection

(define (substring/shared s start . rest)
  (let ((range (%string-range "substring/shared" s (cons start rest))))
    (substring s (car range) (cdr range))))

(define (string-take s n)
  (substring s 0 (%string-count-check "string-take" s n)))

(define (string-take-right s n)
  (let* ((len (string-length s))
         (count (%string-count-check "string-take-right" s n)))
    (substring s (- len count) len)))

(define (string-drop s n)
  (substring s (%string-count-check "string-drop" s n) (string-length s)))

(define (string-drop-right s n)
  (let* ((len (string-length s))
         (count (%string-count-check "string-drop-right" s n)))
    (substring s 0 (- len count))))

(define (string-copy-range s start end)
  (substring s start end))

(define (string-pad s len . rest)
  (let* ((char (if (pair? rest) (car rest) #\space))
         (range (%string-range "string-pad" s
                               (if (pair? rest) (cdr rest) '())))
         (start (car range))
         (end (cdr range))
         (count (- end start))
         (result (make-string len char)))
    (cond ((> count len)
           (string-copy-into result 0 s (- end len) end))
          (else
           (string-copy-into result (- len count) s start end)))
    result))

(define (string-pad-right s len . rest)
  (let* ((char (if (pair? rest) (car rest) #\space))
         (range (%string-range "string-pad-right" s
                               (if (pair? rest) (cdr rest) '())))
         (start (car range))
         (end (cdr range))
         (count (- end start))
         (result (make-string len char)))
    (if (> count len)
        (string-copy-into result 0 s start (+ start len))
        (string-copy-into result 0 s start end))
    result))

(define (string-copy-into target at source start end)
  (let loop ((i 0))
    (if (< (+ start i) end)
        (begin
          (string-set! target (+ at i) (string-ref source (+ start i)))
          (loop (+ i 1))))))

(define (string-trim s . rest)
  (let* ((criterion (if (pair? rest) (car rest) char-whitespace?))
         (range (%string-range "string-trim" s
                               (if (pair? rest) (cdr rest) '())))
         (pred (%string-criterion "string-trim" criterion))
         (start (car range))
         (end (cdr range)))
    (let loop ((i start))
      (if (and (< i end) (pred (string-ref s i)))
          (loop (+ i 1))
          (substring s i end)))))

(define (string-trim-right s . rest)
  (let* ((criterion (if (pair? rest) (car rest) char-whitespace?))
         (range (%string-range "string-trim-right" s
                               (if (pair? rest) (cdr rest) '())))
         (pred (%string-criterion "string-trim-right" criterion))
         (start (car range))
         (end (cdr range)))
    (let loop ((i end))
      (if (and (> i start) (pred (string-ref s (- i 1))))
          (loop (- i 1))
          (substring s start i)))))

(define (string-trim-both s . rest)
  (let* ((criterion (if (pair? rest) (car rest) char-whitespace?))
         (range (%string-range "string-trim-both" s
                               (if (pair? rest) (cdr rest) '())))
         (pred (%string-criterion "string-trim-both" criterion))
         (start (car range))
         (end (cdr range)))
    (let skip-left ((i start))
      (if (and (< i end) (pred (string-ref s i)))
          (skip-left (+ i 1))
          (let skip-right ((j end))
            (if (and (> j i) (pred (string-ref s (- j 1))))
                (skip-right (- j 1))
                (substring s i j)))))))

;;;; Folding, mapping, iteration

(define (string-fold kons knil s . rest)
  (let* ((range (%string-range "string-fold" s rest))
         (end (cdr range)))
    (let loop ((state knil) (i (car range)))
      (if (>= i end)
          state
          (loop (kons (string-ref s i) state) (+ i 1))))))

(define (string-fold-right kons knil s . rest)
  (let ((range (%string-range "string-fold-right" s rest)))
    (let loop ((state knil) (i (- (cdr range) 1)))
      (if (< i (car range))
          state
          (loop (kons (string-ref s i) state) (- i 1))))))

(define (string-map! proc s . rest)
  (let ((range (%string-range "string-map!" s rest)))
    (let loop ((i (car range)))
      (if (< i (cdr range))
          (begin
            (string-set! s i (proc (string-ref s i)))
            (loop (+ i 1)))))
    s))

(define (string-for-each-index proc s . rest)
  (let ((range (%string-range "string-for-each-index" s rest)))
    (let loop ((i (car range)))
      (if (< i (cdr range))
          (begin
            (proc i)
            (loop (+ i 1)))))))

;;;; Alphabetic case

(define (string-map-case who s rest map)
  (let* ((range (%string-range who s rest))
         (result (substring s (car range) (cdr range)))
         (len (string-length result)))
    (let loop ((i 0))
      (if (< i len)
          (begin
            (string-set! result i (map (string-ref result i)))
            (loop (+ i 1)))))
    result))

(define (string-map-case! who s rest map)
  (let ((range (%string-range who s rest)))
    (let loop ((i (car range)))
      (if (< i (cdr range))
          (begin
            (string-set! s i (map (string-ref s i)))
            (loop (+ i 1)))))
    s))

(define (string-upcase s . rest)
  (string-map-case "string-upcase" s rest char-upcase))

(define (string-upcase! s . rest)
  (string-map-case! "string-upcase!" s rest char-upcase))

(define (string-downcase s . rest)
  (string-map-case "string-downcase" s rest char-downcase))

(define (string-downcase! s . rest)
  (string-map-case! "string-downcase!" s rest char-downcase))

(define (string-titlecase-range! s start end)
  (let loop ((i start) (word-start #t))
    (if (< i end)
        (let ((c (string-ref s i)))
          (if (char-alphabetic? c)
              (begin
                (string-set! s i (if word-start
                                     (char-upcase c)
                                     (char-downcase c)))
                (loop (+ i 1) #f))
              (loop (+ i 1) #t))))))

(define (string-titlecase s . rest)
  (let* ((range (%string-range "string-titlecase" s rest))
         (result (substring s (car range) (cdr range))))
    (string-titlecase-range! result 0 (string-length result))
    result))

(define (string-titlecase! s . rest)
  (let ((range (%string-range "string-titlecase!" s rest)))
    (string-titlecase-range! s (car range) (cdr range))
    s))

;;;; Comparison

(define (%string-order who s1 s2 args fold)
  (let* ((range (%string-ranges who s1 s2 args))
         (start1 (car range))
         (end1 (cadr range))
         (start2 (caddr range))
         (end2 (cadddr range)))
    (let loop ((i start1) (j start2))
      (cond ((and (= i end1) (= j end2)) 0)
            ((= i end1) -1)
            ((= j end2) 1)
            (else
             (let ((c1 (fold (string-ref s1 i)))
                   (c2 (fold (string-ref s2 j))))
               (cond ((char=? c1 c2) (loop (+ i 1) (+ j 1)))
                     ((char<? c1 c2) -1)
                     (else 1))))))))

(define (%string-identity c) c)

(define (string-compare s1 s2 proc< proc= proc> . rest)
  (let* ((range (%string-ranges "string-compare" s1 s2 rest))
         (start1 (car range))
         (end1 (cadr range))
         (start2 (caddr range))
         (end2 (cadddr range)))
    (let loop ((i start1) (j start2))
      (cond ((and (= i end1) (= j end2)) (proc= i))
            ((= i end1) (proc< i))
            ((= j end2) (proc> i))
            (else
             (let ((c1 (string-ref s1 i))
                   (c2 (string-ref s2 j)))
               (cond ((char=? c1 c2) (loop (+ i 1) (+ j 1)))
                     ((char<? c1 c2) (proc< i))
                     (else (proc> i)))))))))

(define (string-compare-ci s1 s2 proc< proc= proc> . rest)
  (let* ((range (%string-ranges "string-compare-ci" s1 s2 rest))
         (start1 (car range))
         (end1 (cadr range))
         (start2 (caddr range))
         (end2 (cadddr range)))
    (let loop ((i start1) (j start2))
      (cond ((and (= i end1) (= j end2)) (proc= i))
            ((= i end1) (proc< i))
            ((= j end2) (proc> i))
            (else
             (let ((c1 (char-foldcase (string-ref s1 i)))
                   (c2 (char-foldcase (string-ref s2 j))))
               (cond ((char=? c1 c2) (loop (+ i 1) (+ j 1)))
                     ((char<? c1 c2) (proc< i))
                     (else (proc> i)))))))))

(define (string= s1 s2 . rest)
  (= 0 (%string-order "string=" s1 s2 rest %string-identity)))

(define (string<> s1 s2 . rest)
  (not (= 0 (%string-order "string<>" s1 s2 rest %string-identity))))

(define (string< s1 s2 . rest)
  (= -1 (%string-order "string<" s1 s2 rest %string-identity)))

(define (string> s1 s2 . rest)
  (= 1 (%string-order "string>" s1 s2 rest %string-identity)))

(define (string<= s1 s2 . rest)
  (let ((order (%string-order "string<=" s1 s2 rest %string-identity)))
    (or (= order -1) (= order 0))))

(define (string>= s1 s2 . rest)
  (let ((order (%string-order "string>=" s1 s2 rest %string-identity)))
    (or (= order 1) (= order 0))))

(define (string-ci= s1 s2 . rest)
  (= 0 (%string-order "string-ci=" s1 s2 rest char-foldcase)))

(define (string-ci<> s1 s2 . rest)
  (not (= 0 (%string-order "string-ci<>" s1 s2 rest char-foldcase))))

(define (string-ci< s1 s2 . rest)
  (= -1 (%string-order "string-ci<" s1 s2 rest char-foldcase)))

(define (string-ci> s1 s2 . rest)
  (= 1 (%string-order "string-ci>" s1 s2 rest char-foldcase)))

(define (string-ci<= s1 s2 . rest)
  (let ((order (%string-order "string-ci<=" s1 s2 rest char-foldcase)))
    (or (= order -1) (= order 0))))

(define (string-ci>= s1 s2 . rest)
  (let ((order (%string-order "string-ci>=" s1 s2 rest char-foldcase)))
    (or (= order 1) (= order 0))))
