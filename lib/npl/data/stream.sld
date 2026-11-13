; SRFI 41 streams with the canonical NPL library name.
;
; A stream is a record with a mutable promise box; the head and tail of a
; stream pair are suspended independently. This is the SRFI 41 reference
; model (Philip L. Bewig, MIT), split into primitive and derived sources.
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
  (import (scheme base))
  (include "stream/primitive.scm" "stream/derived.scm"))
