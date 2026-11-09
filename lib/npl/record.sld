; Records beyond R7RS 5.5 (docs/design-data.md section 2).
;
; (record-update type instance (field value) ...) returns a copy of
; instance, which must be of the record type type, with the named fields
; replaced; instance itself is unchanged. An unknown field name is an error
; object raised at run time. define-record-type is re-exported, so this
; library alone is enough to define and update records.
(define-library (npl record)
  (export define-record-type record-update)
  (import (npl primitive)))
