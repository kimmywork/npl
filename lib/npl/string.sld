; SRFI 13 string library with the canonical NPL library name.
;
; The implementation follows the SRFI 13 specification in a compact,
; NPL-specific form. Character-set criteria (SRFI 14) are not available in
; NPL; a criterion is a character or a predicate, and any other value is an
; error. The R5RS string procedures are re-exported from (scheme base) with
; their original identity.
(define-library (npl string)
  (export
    string? make-string string string-length string-ref string-set!
    substring string-append list->string string->list
    string-copy string-copy! string-fill!

    string-null? string-every string-any
    string-tabulate string-unfold string-unfold-right
    reverse-list->string string-join
    substring/shared string-take string-take-right
    string-drop string-drop-right string-pad string-pad-right
    string-trim string-trim-right string-trim-both
    string-compare string-compare-ci
    string= string<> string< string> string<= string>=
    string-ci= string-ci<> string-ci< string-ci> string-ci<= string-ci>=
    string-downcase string-downcase! string-titlecase string-titlecase!
    string-upcase string-upcase!
    string-reverse string-reverse!
    string-concatenate string-concatenate/shared string-append/shared
    string-concatenate-reverse string-concatenate-reverse/shared
    string-map string-map! string-fold string-fold-right
    string-for-each string-for-each-index
    string-prefix-length string-suffix-length
    string-prefix-length-ci string-suffix-length-ci
    string-prefix? string-suffix? string-prefix-ci? string-suffix-ci?
    string-index string-index-right string-skip string-skip-right
    string-count string-contains string-contains-ci
    string-hash string-hash-ci
    string-tokenize string-filter string-delete
    xsubstring string-xcopy! string-replace)
  (import (scheme base) (scheme cxr)
          (only (scheme char)
                char-alphabetic? char-numeric? char-whitespace?
                char-upper-case? char-lower-case?
                char-upcase char-downcase char-foldcase))
  (include "string/kernel.scm" "string/core.scm" "string/search.scm"))
