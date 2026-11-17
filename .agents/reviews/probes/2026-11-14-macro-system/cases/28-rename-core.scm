(import (rename (only (scheme base) if define) (if when-true)))
(write (when-true #t 'yes 'no))
