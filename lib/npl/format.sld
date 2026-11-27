; SRFI 48: Intermediate Format Strings, canonical NPL library.
(define-library (npl format)
  (export format)
  (import (scheme base) (scheme char) (scheme write))
  (include "format/core.scm"))
