; Expect-style fixture runner (the old run_r7rs_fixture.sh in NPL).
;
; A fixture directory contains fixture.meta, a native source, a REPL input,
; expected stdout/stderr/status, and optional Guile oracle settings.  The
; runner compiles and runs the native source, feeds the REPL transcript, and
; compares all three surfaces with the expected files.
(define-library (npl test expect)
  (export run-fixture run-fixture-directory read-text-file
          parse-fixture-meta fixture-meta)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (only (npl string) string-index)
          (only (npl util) path-join)
          (only (npl test util) read-text-file write-text text-lines
                file-equal? trim-newline)
          (npl process)
          (npl unix))
  (begin
    (define (parse-fixture-meta path)
      (let loop ((lines (text-lines (read-text-file path))) (entries '()))
        (if (null? lines)
            (reverse entries)
            (let* ((line (car lines))
                   (equal (string-index line #\=)))
              (if (not equal)
                  (loop (cdr lines) entries)
                  (loop (cdr lines)
                        (cons (cons (substring line 0 equal)
                                    (substring line (+ equal 1)
                                               (string-length line)))
                              entries)))))))

    (define (fixture-meta meta key . rest)
      (let ((entry (assoc key meta)))
        (if entry
            (cdr entry)
            (if (null? rest) #f (car rest)))))

    (define (required-meta meta key)
      (let ((value (fixture-meta meta key)))
        (if value
            value
            (error "fixture.meta: missing key" key))))

    (define (surface-pass? name surface expected-out expected-err
                           expected-status status output error)
      (cond
        ((not (= status expected-status))
         (display "fixture=" (current-error-port))
         (display name (current-error-port))
         (display " surface=" (current-error-port))
         (display surface (current-error-port))
         (display " field=status expected=" (current-error-port))
         (display expected-status (current-error-port))
         (display " actual=" (current-error-port))
         (display status (current-error-port))
         (display " classification=unclassified" (current-error-port))
         (newline (current-error-port))
         #f)
        ((not (file-equal? expected-out output))
         (display "fixture=" (current-error-port))
         (display name (current-error-port))
         (display " surface=" (current-error-port))
         (display surface (current-error-port))
         (display " field=stdout classification=unclassified"
                  (current-error-port))
         (newline (current-error-port))
         #f)
        ((not (file-equal? expected-err error))
         (display "fixture=" (current-error-port))
         (display name (current-error-port))
         (display " surface=" (current-error-port))
         (display surface (current-error-port))
         (display " field=stderr classification=unclassified"
                  (current-error-port))
         (newline (current-error-port))
         #f)
        (else #t)))

    (define (separator? character)
      (or (char=? character #\space)
          (char=? character #\tab)
          (char=? character #\newline)
          (char=? character #\return)))

    ; fixture.meta stores a whitespace-separated list of affected versions.
    (define (version-listed? versions version)
      (let ((text (trim-newline versions))
            (wanted (trim-newline version)))
        (let loop ((index 0))
          (cond
            ((>= index (string-length text)) #f)
            ((separator? (string-ref text index)) (loop (+ index 1)))
            (else
              (let ((end
                      (let scan ((at index))
                        (if (if (< at (string-length text))
                                (not (separator? (string-ref text at)))
                                #f)
                            (scan (+ at 1))
                            at))))
                (if (string=? (substring text index end) wanted)
                    #t
                    (loop end))))))))

    (define (guile-version)
      (let* ((directory (make-temporary-directory))
             (output (path-join directory "version"))
             (error (path-join directory "error"))
             (status (run-process* (list "guile" "-c" "(display (version))")
                                  #f output error)))
        (if (= status 0)
            (let ((value (read-text-file output)))
              (delete-file output)
              (delete-file error)
              (delete-directory directory)
              value)
            (begin
              (delete-file output)
              (delete-file error)
              (delete-directory directory)
              #f))))

    (define (run-fixture-directory directory nplc npli)
      (let* ((meta (parse-fixture-meta (path-join directory "fixture.meta")))
             (name (required-meta meta "name"))
             (native (path-join directory (required-meta meta "native")))
             (repl (path-join directory (required-meta meta "repl")))
             (expected-out (path-join directory
                                     (required-meta meta "expected-out")))
             (expected-err (path-join directory
                                     (required-meta meta "expected-err")))
             (expected-status
               (string->number (required-meta meta "expected-status")))
             (oracle (fixture-meta meta "oracle" "guile"))
             (guile-defect (fixture-meta meta "guile-defect" ""))
             (work (make-temporary-directory))
             (native-out (path-join work "native.out"))
             (native-err (path-join work "native.err"))
             (repl-out (path-join work "repl.out"))
             (repl-err (path-join work "repl.err"))
             (guile-out (path-join work "guile.out"))
             (guile-err (path-join work "guile.err"))
             (native-bin (path-join work "native"))
             (repl-input repl)
             (oracle-skip (if (string=? oracle "none") "oracle-unavailable" #f)))
        (if (fixture-meta meta "repl-body")
            (let ((combined (path-join work "repl.in")))
              (write-text
                combined
                (string-append
                  (read-text-file repl)
                  (read-text-file (path-join directory
                                             (fixture-meta meta "repl-body")))
                  "\n(exit)\n"))
              (set! repl-input combined))
            #f)
        (let ((compile-status
                (run-process*
                  (list nplc native "-o" native-bin)
                  #f
                  (path-join work "compile.out")
                  (path-join work "compile.err"))))
          (if (not (= compile-status 0))
              (begin
                (display "fixture=" (current-error-port))
                (display name (current-error-port))
                (display " surface=native phase=compile status=FAIL"
                         (current-error-port))
                (newline (current-error-port))
                #f)
              (let ((native-status
                      (run-process* (list native-bin) #f native-out native-err))
                    (repl-status
                      (run-process* (list npli "--no-prompt")
                                   repl-input repl-out repl-err)))
                (if (if oracle-skip #f (if guile-defect #t #f))
                    (let ((version (guile-version)))
                      (if (if version
                              (version-listed? guile-defect version)
                              #f)
                          (set! oracle-skip "oracle-known-defect")
                          #f))
                    #f)
                (let ((guile-status
                        (if oracle-skip
                            0
                            (run-process*
                              (list "guile" "--r7rs" "-q"
                                    "--no-auto-compile" "-s"
                                    (if (string=? oracle "none")
                                        native
                                        (path-join directory
                                                   (fixture-meta meta "guile"
                                                                 "native.npl"))))
                              #f guile-out guile-err))))
                  (let ((native-ok
                          (surface-pass? name "native" expected-out
                                         expected-err expected-status
                                         native-status native-out native-err))
                        (repl-ok
                          (surface-pass? name "repl" expected-out
                                         expected-err expected-status
                                         repl-status repl-out repl-err))
                        (guile-ok
                          (if oracle-skip
                              #t
                              (surface-pass? name "guile" expected-out
                                             expected-err expected-status
                                             guile-status guile-out guile-err))))
                    (if (if native-ok (if repl-ok guile-ok #f) #f)
                        (begin
                          (display "fixture=") (display name)
                          (display " surface=native status=PASS")
                          (display " classification=matched-expected") (newline)
                          (display "fixture=") (display name)
                          (display " surface=repl status=PASS")
                          (display " classification=matched-expected") (newline)
                          (if oracle-skip
                              (begin
                                (display "fixture=") (display name)
                                (display " surface=guile status=SKIP")
                                (display " classification=") (display oracle-skip)
                                (newline))
                              (begin
                                (display "fixture=") (display name)
                                (display " surface=guile status=PASS")
                                (display " classification=matched-expected")
                                (newline)))
                          (if (if (file-equal? native-out repl-out)
                                  (if oracle-skip
                                      #t
                                      (file-equal? native-out guile-out))
                                  #f)
                              (begin
                                (display "fixture=") (display name)
                                (display " surfaces=native,repl")
                                (if (not oracle-skip) (display ",guile"))
                                (display " status=PASS")
                                (display " classification=identical-observable-result")
                                (newline)
                                #t)
                              (begin
                                (display "fixture=" (current-error-port))
                                (display name (current-error-port))
                                (display " surfaces=native,repl"
                                         (current-error-port))
                                (if (not oracle-skip) (display ",guile"
                                                               (current-error-port)))
                                (display " field=observable-result"
                                         (current-error-port))
                                (display " classification=unclassified"
                                         (current-error-port))
                                (newline (current-error-port))
                                #f)))
                        #f))))))))

    (define (run-fixture directory . rest)
      (let ((nplc (if (null? rest) "build/bin/nplc" (car rest)))
            (npli (if (if (null? rest) #t (null? (cdr rest)))
                      "build/bin/npli"
                      (car (cdr rest)))))
        (run-fixture-directory directory nplc npli)))))
