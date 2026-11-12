; SRFI 13 compatibility alias for (npl string).
(define-library (srfi 13)
  (export
    string? make-string string string-length string-ref string-set! substring
    string-append list->string string->list string-copy string-copy!
    string-fill! string-null? string-every string-any string-tabulate
    string-unfold string-unfold-right reverse-list->string string-join
    substring/shared string-take string-take-right string-drop
    string-drop-right string-pad string-pad-right string-trim
    string-trim-right string-trim-both string-compare string-compare-ci
    string= string<> string< string> string<= string>= string-ci= string-ci<>
    string-ci< string-ci> string-ci<= string-ci>= string-downcase
    string-downcase! string-titlecase string-titlecase! string-upcase
    string-upcase! string-reverse string-reverse! string-concatenate
    string-concatenate/shared string-append/shared string-concatenate-reverse
    string-concatenate-reverse/shared string-map string-map! string-fold
    string-fold-right string-for-each string-for-each-index
    string-prefix-length string-suffix-length string-prefix-length-ci
    string-suffix-length-ci string-prefix? string-suffix? string-prefix-ci?
    string-suffix-ci? string-index string-index-right string-skip
    string-skip-right string-count string-contains string-contains-ci
    string-hash string-hash-ci string-tokenize string-filter string-delete
    xsubstring string-xcopy! string-replace)
  (import (npl string)))
