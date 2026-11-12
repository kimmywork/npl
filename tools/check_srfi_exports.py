#!/usr/bin/env python3
"""Check that every (srfi N) alias exports exactly its canonical (npl ...) library.

The alias libraries are hand-written re-exports; this catches a missing or
extra name, which the runtime loader cannot see because alias imports only
resolve the names that happen to be present.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

ALIASES = {
    "lib/srfi/1.sld": "lib/npl/list.sld",
    "lib/srfi/111.sld": "lib/npl/data/box.sld",
    "lib/srfi/125.sld": "lib/npl/hashtable.sld",
    "lib/srfi/189.sld": "lib/npl/data/option.sld",
    "lib/srfi/196.sld": "lib/npl/data/range.sld",
}


def tokenize(text):
    text = text.replace("\r", "\n")
    tokens = []
    i = 0
    while i < len(text):
        c = text[i]
        if c in " \t\n":
            i += 1
        elif c == ";":
            while i < len(text) and text[i] != "\n":
                i += 1
        elif c in "()":
            tokens.append(c)
            i += 1
        elif c == '"':
            j = i + 1
            while j < len(text):
                if text[j] == "\\":
                    j += 2
                elif text[j] == '"':
                    j += 1
                    break
                else:
                    j += 1
            tokens.append(text[i:j])
            i = j
        else:
            j = i
            while j < len(text) and text[j] not in " \t\n();":
                j += 1
            tokens.append(text[i:j])
            i = j
    return tokens


def parse(tokens):
    def form(index):
        if tokens[index] == "(":
            values = []
            index += 1
            while tokens[index] != ")":
                value, index = form(index)
                values.append(value)
            return values, index + 1
        return tokens[index], index + 1

    values = []
    index = 0
    while index < len(tokens):
        value, index = form(index)
        values.append(value)
    return values


def exports(path):
    forms = parse(tokenize(Path(path).read_text()))
    for definition in forms:
        if isinstance(definition, list) and definition and definition[0] == "define-library":
            for declaration in definition:
                if isinstance(declaration, list) and declaration and declaration[0] == "export":
                    names = set()
                    for item in declaration[1:]:
                        if isinstance(item, str):
                            names.add(item)
                        elif isinstance(item, list) and len(item) == 3 and item[0] == "rename":
                            names.add(item[2])
                        else:
                            raise SystemExit("%s: unsupported export item %r" % (path, item))
                    return names
    raise SystemExit("%s: no export declaration" % path)


def main():
    failed = False
    for alias, canonical in ALIASES.items():
        expected = exports(ROOT / canonical)
        actual = exports(ROOT / alias)
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        if missing or extra:
            failed = True
            print("%s != %s" % (alias, canonical))
            if missing:
                print("  missing: " + " ".join(missing))
            if extra:
                print("  extra:   " + " ".join(extra))
    option = exports(ROOT / "lib/npl/data/option.sld")
    maybe_either = (exports(ROOT / "lib/npl/data/maybe.sld") |
                    exports(ROOT / "lib/npl/data/either.sld"))
    missing = sorted(option - maybe_either)
    extra = sorted(maybe_either - option)
    if missing or extra:
        failed = True
        print("(npl data maybe) + (npl data either) != (npl data option)")
        if missing:
            print("  missing: " + " ".join(missing))
        if extra:
            print("  extra:   " + " ".join(extra))

    if failed:
        return 1
    print("SRFI aliases: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
