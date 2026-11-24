; R17: (npl unix) used to re-export (npl process)'s delete-directory; it now
; defines its own, so importing both libraries is a binding conflict.
; Expected: "ok" (it was accepted at 32cfa51).
(import (scheme base) (scheme write) (npl process) (npl unix))
(display "ok")
(newline)
