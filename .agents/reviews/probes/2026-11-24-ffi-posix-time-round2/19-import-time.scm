; R19: imported by run.sh --cache with NPL_CC=/usr/bin/false, once without and once
; with NPL_C_STRUCT_LAYOUTS pointing at a package cache (19-package.meta).
(import (scheme base) (scheme write) (npl time))
(display (time-second (make-time time-utc 0 5)))
(newline)
