; SRFI 44 compatibility alias for (npl data collection).
(define-library (srfi 44)
  (export
    collection? collection-name collection-size collection-empty?
    collection-count collection-get-any collection->list collection-copy
    collection-clear collection-clear! collection-fold-left
    collection-fold-right collection-fold-keys-left collection-fold-keys-right
    ordered-collection? limited-collection? directional-collection?
    purely-mutable-collection? sequence? flexible-sequence? bag? set? map?
    dictionary? list list? make-list list-copy list->list list-size
    list-empty? list-contains? list-ref list-get-any list-get-left
    list-get-right list-count list= list-fold-left list-fold-right
    list-equivalence-function list-add list-add! list-set list-set!
    list-insert-left list-insert-left! list-insert-right list-insert-right!
    list-delete list-delete! list-delete-left list-delete-left!
    list-delete-right list-delete-right! list-delete-all list-delete-all!
    list-clear list-clear! list-insert list-insert! list-delete-at
    list-delete-at! list-replace-from list-replace-from! vector vector?
    make-vector vector-copy vector->list vector-size vector-empty?
    vector-contains? vector-count vector-ref vector-get-any vector-get-left
    vector-get-right vector-set vector-set! vector-replace-from
    vector-replace-from! vector= vector-fold-left vector-fold-right
    vector-equivalence-function string string? make-string string-copy
    string->list string-size string-empty? string-contains? string-count
    string-ref string-get-any string-get-left string-get-right string-set
    string-set! string-replace-from string-replace-from! string=
    string-fold-left string-fold-right string-equivalence-function
    sequence-ref sequence-size sequence-get-left sequence-get-right)
  (import (npl data collection)))
