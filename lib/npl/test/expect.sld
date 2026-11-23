; Expect-style fixture runner (the old run_r7rs_fixture.sh in NPL).
;
; A fixture directory contains fixture.meta, a native source, a REPL input,
; expected stdout/stderr/status, and optional Guile oracle settings.  The
; runner compiles and runs the native source, feeds the REPL transcript, and
; compares all three surfaces with the expected files.
(define-library (npl test expect)
  (export run-fixture run-fixture-directory read-text-file write-text-file
          parse-fixture-meta fixture-meta)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (only (npl internal primitive) read-file)
          (npl process)
          (npl unix))
  (begin
    (define (join-path directory name)
      (if (if (= (string-length directory) 0)
              #t
              (char=? (string-ref directory
                                  (- (string-length directory) 1))
                      #\/))
          (string-append directory name)
          (string-append directory "/" name)))

    (define (read-text-file path)
      (read-file path))

    (define (write-text-file path text)
      (call-with-output-file path
        (lambda (port) (display text port))))

    (define (string-index-char text character)
      (let loop ((index 0))
        (cond
          ((= index (string-length text)) #f)
          ((char=? (string-ref text index) character) index)
          (else (loop (+ index 1))))))

    (define (line-list text)
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

    (define (parse-fixture-meta path)
      (let loop ((lines (line-list (read-text-file path))) (entries '()))
        (if (null? lines)
            (reverse entries)
            (let* ((line (car lines))
                   (equal (string-index-char line #\=)))
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

    (define (run-capture arguments input output error)
      (run-process* arguments input output error))

    (define (files-equal? left right)
      (string=? (read-text-file left) (read-text-file right)))

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
        ((not (files-equal? expected-out output))
         (display "fixture=" (current-error-port))
         (display name (current-error-port))
         (display " surface=" (current-error-port))
         (display surface (current-error-port))
         (display " field=stdout classification=unclassified"
                  (current-error-port))
         (newline (current-error-port))
         #f)
        ((not (files-equal? expected-err error))
         (display "fixture=" (current-error-port))
         (display name (current-error-port))
         (display " surface=" (current-error-port))
         (display surface (current-error-port))
         (display " field=stderr classification=unclassified"
                  (current-error-port))
         (newline (current-error-port))
         #f)
        (else #t)))

    (define (guile-version)
      (let* ((directory (make-temporary-directory))
             (output (join-path directory "version"))
             (error (join-path directory "error"))
             (status (run-capture (list "guile" "-c" "(display (version))")
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
      (let* ((meta (parse-fixture-meta (join-path directory "fixture.meta")))
             (name (required-meta meta "name"))
             (native (join-path directory (required-meta meta "native")))
             (repl (join-path directory (required-meta meta "repl")))
             (expected-out (join-path directory
                                     (required-meta meta "expected-out")))
             (expected-err (join-path directory
                                     (required-meta meta "expected-err")))
             (expected-status
               (string->number (required-meta meta "expected-status")))
             (oracle (fixture-meta meta "oracle" "guile"))
             (guile-defect (fixture-meta meta "guile-defect" ""))
             (work (make-temporary-directory))
             (native-out (join-path work "native.out"))
             (native-err (join-path work "native.err"))
             (repl-out (join-path work "repl.out"))
             (repl-err (join-path work "repl.err"))
             (guile-out (join-path work "guile.out"))
             (guile-err (join-path work "guile.err"))
             (native-bin (join-path work "native"))
             (repl-input repl)
             (oracle-skip (if (string=? oracle "none") "oracle-unavailable" #f)))
        (if (fixture-meta meta "repl-body")
            (let ((combined (join-path work "repl.in")))
              (write-text-file
                combined
                (string-append
                  (read-text-file repl)
                  (read-text-file (join-path directory
                                             (fixture-meta meta "repl-body")))
                  "\n(exit)\n"))
              (set! repl-input combined))
            #f)
        (let ((compile-status
                (run-capture
                  (list nplc native "-o" native-bin)
                  #f
                  (join-path work "compile.out")
                  (join-path work "compile.err"))))
          (if (not (= compile-status 0))
              (begin
                (display "fixture=" (current-error-port))
                (display name (current-error-port))
                (display " surface=native phase=compile status=FAIL"
                         (current-error-port))
                (newline (current-error-port))
                #f)
              (let ((native-status
                      (run-capture (list native-bin) #f native-out native-err))
                    (repl-status
                      (run-capture (list npli "--no-prompt")
                                   repl-input repl-out repl-err)))
                (if (if oracle-skip #f (if guile-defect #t #f))
                    (let ((version (guile-version)))
                      (if (if version (string=? version guile-defect) #f)
                          (set! oracle-skip "oracle-known-defect")
                          #f))
                    #f)
                (let ((guile-status
                        (if oracle-skip
                            0
                            (run-capture
                              (list "guile" "--r7rs" "-q"
                                    "--no-auto-compile" "-s"
                                    (if (string=? oracle "none")
                                        native
                                        (join-path directory
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
                          (if (if (files-equal? native-out repl-out)
                                  (if oracle-skip
                                      #t
                                      (files-equal? native-out guile-out))
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
