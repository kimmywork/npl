(define-library (scheme char)
  (export (rename char-ci-equal char-ci=?)
          (rename char-ci-less char-ci<?)
          (rename char-ci-greater char-ci>?)
          (rename char-ci-less-equal char-ci<=?)
          (rename char-ci-greater-equal char-ci>=?)
          char-alphabetic? char-numeric? char-whitespace?
          char-upper-case? char-lower-case?
          digit-value char-upcase char-downcase char-foldcase
          string-upcase string-downcase string-foldcase
          (rename string-ci-equal string-ci=?)
          (rename string-ci-less string-ci<?)
          (rename string-ci-greater string-ci>?)
          (rename string-ci-less-equal string-ci<=?)
          (rename string-ci-greater-equal string-ci>=?))
  (import (only (scheme base)
                + - < <= = > >= and or not null? pair? car cdr map apply cons
                append reverse char? char->integer integer->char
                string->list list->string string-length string-ref
                char=? char<? char>? char<=? char>=?
                string=? string<? string>? string<=? string>=?)
          (only (npl primitive)
                npl_char_property npl_char_digit_value npl_char_case
                npl_char_full_case npl_string_full_case))
  (begin
    ; Properties and mappings are Unicode table lookups (runtime/unicode.c,
    ; tables from tools/gen_unicode.py); R7RS 6.6 names the properties.
    (define (char-alphabetic? c) (npl_char_property c 0))
    (define (char-numeric? c) (npl_char_property c 1))
    (define (char-whitespace? c) (npl_char_property c 2))
    (define (char-upper-case? c) (npl_char_property c 3))
    (define (char-lower-case? c) (npl_char_property c 4))
    (define (digit-value c) (npl_char_digit_value c))
    (define (char-upcase c) (npl_char_case c 0))
    (define (char-downcase c) (npl_char_case c 1))
    (define (char-foldcase c) (npl_char_case c 2))

    (define (char-ci-chain compare a b rest)
      (let loop ((previous (char-foldcase a)) (items (cons b rest)))
        (if (null? items)
            #t
            (let ((next (char-foldcase (car items))))
              (if (compare previous next) (loop next (cdr items)) #f)))))
    (define (char-ci-equal a b . rest) (char-ci-chain char=? a b rest))
    (define (char-ci-less a b . rest) (char-ci-chain char<? a b rest))
    (define (char-ci-greater a b . rest) (char-ci-chain char>? a b rest))
    (define (char-ci-less-equal a b . rest) (char-ci-chain char<=? a b rest))
    (define (char-ci-greater-equal a b . rest)
      (char-ci-chain char>=? a b rest))

    ; Full string case mappings (R7RS 6.7): each character's full mapping,
    ; in order (npl_string_full_case, runtime/unicode.c, for upcase and
    ; foldcase). string-downcase applies the Final_Sigma rule (Unicode 3.13):
    ; capital sigma becomes final sigma when a Cased character precedes it
    ; and none follows (Case_Ignorable characters skipped). Guile and Racket
    ; both apply it.
    (define (string-upcase s) (npl_string_full_case s 0))
    (define (string-foldcase s) (npl_string_full_case s 2))

    (define sigma (integer->char #x3A3))
    (define final-sigma (integer->char #x3C2))
    (define (cased? c) (npl_char_property c 5))
    (define (case-ignorable? c) (npl_char_property c 6))
    (define (cased-before? characters index)
      (let loop ((i (- index 1)))
        (if (< i 0)
            #f
            (let ((c (string-ref characters i)))
              (if (case-ignorable? c) (loop (- i 1)) (cased? c))))))
    (define (cased-after? characters index)
      (let ((length (string-length characters)))
        (let loop ((i (+ index 1)))
          (if (>= i length)
              #f
              (let ((c (string-ref characters i)))
                (if (case-ignorable? c) (loop (+ i 1)) (cased? c)))))))
    (define (string-downcase s)
      (let ((length (string-length s)))
        (let loop ((index 0) (output '()))
          (if (= index length)
              (list->string (reverse output))
              (let ((c (string-ref s index)))
                (loop (+ index 1)
                      (if (and (char=? c sigma) (cased-before? s index)
                               (not (cased-after? s index)))
                          (cons final-sigma output)
                          (append (reverse (npl_char_full_case c 1))
                                  output))))))))

    (define (fold-all strings) (map string-foldcase strings))
    (define (string-ci-equal a b . rest)
      (apply string=? (fold-all (cons a (cons b rest)))))
    (define (string-ci-less a b . rest)
      (apply string<? (fold-all (cons a (cons b rest)))))
    (define (string-ci-greater a b . rest)
      (apply string>? (fold-all (cons a (cons b rest)))))
    (define (string-ci-less-equal a b . rest)
      (apply string<=? (fold-all (cons a (cons b rest)))))
    (define (string-ci-greater-equal a b . rest)
      (apply string>=? (fold-all (cons a (cons b rest)))))))
