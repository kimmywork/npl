#!/usr/bin/env python3
"""Adapt chibi's r7rs-tests.scm to an nplc program.

nplc programs are definitions plus (define (main) ...): each top-level
expression becomes (define _tN (top-level-form (lambda () expr))), which
runs in order like a top-level command and reports a raised error as the
REPL does, without stopping the run. Definitions, syntax definitions, and
uses of the suite's definition-producing macros stay as written. The one
top-level begin that mixes a definition-producing macro with a test keeps
its definitions and wraps only the test.

Usage: native_adapt.py r7rs-tests.scm out.npl
"""
import re
import sys

TEST_MACROS = {"test", "test-values", "test-assert", "test-error",
               "test-numeric-syntax", "test-write-syntax", "test-precision"}


def top_level_forms(src):
    forms, i, n = [], 0, len(src)

    def skip(i):
        while i < n:
            if src[i] in " \t\r\n\f":
                i += 1
            elif src[i] == ";":
                while i < n and src[i] != "\n":
                    i += 1
            elif src.startswith("#|", i):
                depth, i = 1, i + 2
                while i < n and depth:
                    if src.startswith("|#", i):
                        depth, i = depth - 1, i + 2
                    elif src.startswith("#|", i):
                        depth, i = depth + 1, i + 2
                    else:
                        i += 1
            else:
                break
        return i

    def end_of(i):
        if src[i] != "(":
            j = i
            while j < n and src[j] not in " \t\r\n()":
                j += 1
            return j
        j, depth = i + 1, 1
        while depth:
            c = src[j]
            if c == '"':
                j += 1
                while src[j] != '"':
                    j += 2 if src[j] == "\\" else 1
            elif c == ";":
                while src[j] != "\n":
                    j += 1
            elif src.startswith("#\\", j):
                j += 3
                continue
            elif src.startswith("#|", j):
                d, j = 1, j + 2
                while d:
                    if src.startswith("|#", j):
                        d, j = d - 1, j + 2
                    elif src.startswith("#|", j):
                        d, j = d + 1, j + 2
                    else:
                        j += 1
                continue
            elif c == "|":
                j += 1
                while src[j] != "|":
                    j += 2 if src[j] == "\\" else 1
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            j += 1
        return j

    i = skip(0)
    while i < n:
        j = end_of(i)
        forms.append(src[i:j])
        i = skip(j)
    return forms


def main():
    src = open(sys.argv[1]).read()
    forms = top_level_forms(src)
    macros = set(re.findall(r"^\(define-syntax\s+([^\s()]+)", "\n".join(forms),
                            re.M)) - TEST_MACROS
    out, count = [], 0
    for form in forms:
        head = form[1:].split(None, 1)[0].rstrip(")") if form.startswith("(") else ""
        if head == "begin" and "(test " in form:
            out.append(re.sub(r"\(test (.*)\)\)\s*$",
                              lambda m: "(define _tbegin (test %s)))" % m.group(1),
                              form, flags=re.S))
        elif head in ("import", "define", "define-syntax", "define-record-type",
                      "define-values") or head in macros:
            out.append(form)
        else:
            count += 1
            out.append("(define _t%d (top-level-form (lambda () %s)))" % (count, form))
    out.insert(1, """(define (top-level-form thunk)
  (guard (e (#t (display "error: " (current-error-port))
               (display (if (error-object? e) (error-object-message e) e)
                        (current-error-port))
               (newline (current-error-port))))
    (thunk)))""")
    out.append("(define (main) (test-exit) 0)")
    open(sys.argv[2], "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
