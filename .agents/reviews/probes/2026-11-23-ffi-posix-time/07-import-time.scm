; R6: importing (npl time) compiles and runs a C layout probe.
; run.sh --cc runs this with NPL_CC=/usr/bin/false and times the import.
(import (scheme base) (scheme write) (npl time))
(display "imported") (newline)
