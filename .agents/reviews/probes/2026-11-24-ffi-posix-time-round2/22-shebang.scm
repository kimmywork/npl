#!/usr/bin/env npli
; R22: npli accepts a leading shebang (docs/toolchain.md); nplc rejects the
; same file, and its diagnostic gives the wrong line.
(import (scheme base) (scheme write))
(display "ok")
(newline)
