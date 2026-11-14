(import (scheme write) (prefix (scheme base) b:))
(b:if #t (write (quote yes)) (write (quote no)))
