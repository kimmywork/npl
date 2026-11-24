; Shared helpers for the NPL gate/tooling libraries.  Keeping them here
; avoids a private copy in every tools/*.sld file.  Paths are joined with
; (npl util) path-join, substrings are found with (npl string)
; string-contains, and processes run through (npl process) run-process*;
; this library adds only what those do not provide.
(define-library (npl test util)
  (export write-text read-text-file text-lines file-size file-empty?
          file-equal? expect remove-tree run-ok? run-status
          with-directory with-environment trim-newline)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (only (npl internal primitive) read-file)
          (only (npl process) run-process*)
          (only (npl unix) file-info file-info:size current-directory
                set-current-directory! set-environment-variable!
                delete-environment-variable!))
  (begin
    (define (write-text path text)
      (call-with-output-file path
        (lambda (port) (display text port))))

    ; The public name for the runtime's whole-file read.
    (define (read-text-file path) (read-file path))

    ; Lines without their terminators; a final line without one is kept.
    (define (text-lines text)
      (let loop ((start 0) (index 0) (lines '()))
        (cond
          ((= index (string-length text))
           (reverse (if (= start index)
                        lines
                        (cons (substring text start index) lines))))
          ((char=? (string-ref text index) #\newline)
           (loop (+ index 1) (+ index 1)
                 (cons (substring text start index) lines)))
          (else (loop start (+ index 1) lines)))))

    (define (file-size path)
      (file-info:size (file-info path #t)))

    (define (file-empty? path)
      (= (file-size path) 0))

    (define (file-equal? left right)
      (string=? (read-text-file left) (read-text-file right)))

    (define (expect ok name condition)
      (if condition
          ok
          (begin
            (display "tools: FAIL ")
            (display name)
            (newline)
            #f)))

    (define (run-ok? args input output error)
      (= (run-process* args input output error) 0))

    (define (run-status args)
      (run-process* args #f "/dev/null" "/dev/null"))

    (define (remove-tree directory)
      (if (file-exists? directory)
          (begin
            (run-process* (list "rm" "-rf" directory) #f #f #f)
            #f)
          #f))

    (define (with-directory directory thunk)
      (let ((saved (current-directory)))
        (dynamic-wind
          (lambda () (set-current-directory! directory))
          thunk
          (lambda () (set-current-directory! saved)))))

    (define (with-environment name value thunk)
      (let ((saved (get-environment-variable name)))
        (dynamic-wind
          (lambda ()
            (if value
                (set-environment-variable! name value)
                (delete-environment-variable! name)))
          thunk
          (lambda ()
            (if saved
                (set-environment-variable! name saved)
                (delete-environment-variable! name))))))

    (define (trim-newline text)
      (let loop ((end (string-length text)))
        (if (if (> end 0)
                (char=? (string-ref text (- end 1)) #\newline)
                #f)
            (loop (- end 1))
            (substring text 0 end))))))
