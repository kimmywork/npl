; SRFI 140 compatibility alias for (npl string immutable).
(define-library (srfi 140)
  (export
    string? istring? string-null? string-every string-any string->vector
    string->list vector->string list->string reverse-list->string string->utf8
    string->utf16 string->utf16be string->utf16le utf8->string utf16->string
    utf16be->string utf16le->string string string-append string-tabulate
    string-unfold string-unfold-right string-length string-ref substring
    string-take string-take-right string-drop string-drop-right string-pad
    string-pad-right string-trim string-trim-right string-trim-both
    string-replace string-copy string-copy! string-fill! string-set!
    string-concatenate string-concatenate-reverse string-join string-fold
    string-fold-right string-map string-map-index string-for-each
    string-for-each-index string-contains string-contains-right string-index
    string-index-right string-skip string-skip-right string-prefix?
    string-suffix? string-prefix-length string-suffix-length string-count
    string-filter string-remove string-split string-tokenize string-replicate
    string-repeat xsubstring string-upcase string-downcase string-foldcase
    string-titlecase)
  (import (npl string immutable)))
