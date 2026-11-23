; Top-level program: inside define-library, Guile 3.0's (scheme r5rs) binds
; neither else (for case/cond) nor include; at top level both are core.
(import (scheme r5rs))
(include "behavior.scm")
