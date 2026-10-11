#!/usr/bin/env python3
"""Generate NPL's Unicode tables.

Usage: python3 tools/gen_unicode.py [--ucd DIR]

Writes runtime/unicode_tables.h (character properties and case mappings
for the runtime) at the Unicode version of the host Python's unicodedata
(docs/design-l6.md, Decision 4); the version is recorded in the output.
Derived properties that unicodedata does not expose (Alphabetic, Uppercase,
Lowercase, White_Space) and the full case mappings come from the Unicode
Character Database files of that same version: from --ucd DIR, or
downloaded from unicode.org into a temporary directory. The generator
checks that the files' version matches unicodedata. Regenerate after a
Python upgrade and commit the result.
"""

import argparse
import os
import tempfile
import unicodedata
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = unicodedata.unidata_version
FILES = ["UnicodeData.txt", "DerivedCoreProperties.txt", "PropList.txt",
         "CaseFolding.txt", "SpecialCasing.txt"]

# Characters `write` prints with hex escapes, as Guile does: the general
# categories Cc Cf Cs Co Cn Zs Zl Zp, except U+0020 SPACE.
NONGRAPHIC = {"Cc", "Cf", "Cs", "Co", "Cn", "Zs", "Zl", "Zp"}


def fetch(directory):
    for name in FILES:
        path = os.path.join(directory, name)
        if not os.path.exists(path):
            url = "https://www.unicode.org/Public/%s/ucd/%s" % (VERSION, name)
            urllib.request.urlretrieve(url, path)
    with open(os.path.join(directory, "DerivedCoreProperties.txt")) as data:
        header = data.readline()
    if ("-%s.txt" % VERSION) not in header:
        raise SystemExit("UCD version mismatch: %s vs unicodedata %s"
                         % (header.strip(), VERSION))


def records(path):
    with open(path) as data:
        for line in data:
            line = line.split("#")[0].strip()
            if line:
                yield [field.strip() for field in line.split(";")]


def code_range(text):
    if ".." in text:
        low, high = text.split("..")
        return int(low, 16), int(high, 16)
    return int(text, 16), int(text, 16)


def merge(pairs):
    merged = []
    for low, high in sorted(pairs):
        if merged and low <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(high, merged[-1][1]))
        else:
            merged.append((low, high))
    return merged


def property_ranges(path, name):
    return merge(code_range(r[0]) for r in records(path) if r[1] == name)


def predicate_ranges(predicate):
    result, start = [], None
    for code in range(0x110000):
        inside = predicate(code)
        if inside and start is None:
            start = code
        elif not inside and start is not None:
            result.append((start, code - 1))
            start = None
    if start is not None:
        result.append((start, 0x10FFFF))
    return result


def range_table(name, items):
    lines = ["static const uint32_t %s[][2] = {" % name]
    row = []
    for low, high in items:
        row.append("{0x%X, 0x%X}," % (low, high))
        if len(row) == 4:
            lines.append("    " + " ".join(row))
            row = []
    if row:
        lines.append("    " + " ".join(row))
    lines.append("};")
    return "\n".join(lines)


def pair_table(name, items):
    lines = ["static const uint32_t %s[][2] = {" % name]
    row = []
    for source, target in sorted(items):
        row.append("{0x%X, 0x%X}," % (source, target))
        if len(row) == 4:
            lines.append("    " + " ".join(row))
            row = []
    if row:
        lines.append("    " + " ".join(row))
    lines.append("};")
    return "\n".join(lines)


def full_table(name, items):
    """Rows of {source, count, c1, c2, c3} for full mappings, sorted."""
    lines = ["static const uint32_t %s[][5] = {" % name]
    for source, targets in sorted(items):
        cells = targets + [0] * (3 - len(targets))
        lines.append("    {0x%X, %d, 0x%X, 0x%X, 0x%X}," %
                     (source, len(targets), cells[0], cells[1], cells[2]))
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ucd")
    options = parser.parse_args()
    directory = options.ucd or tempfile.mkdtemp(prefix="npl-ucd-")
    fetch(directory)
    path = lambda name: os.path.join(directory, name)

    upper, lower, decimal = [], [], []
    for r in records(path("UnicodeData.txt")):
        code = int(r[0], 16)
        if r[12]:
            upper.append((code, int(r[12], 16)))
        if r[13]:
            lower.append((code, int(r[13], 16)))
        if r[2] == "Nd":
            decimal.append((code, int(r[6])))
    # Simple case folding: status C and S.
    fold = [(int(r[0], 16), int(r[2], 16))
            for r in records(path("CaseFolding.txt")) if r[1] in ("C", "S")]
    # Full mappings that differ from the simple ones: unconditional
    # SpecialCasing entries (no condition list) and full folding (F).
    full_upper, full_lower = [], []
    for r in records(path("SpecialCasing.txt")):
        if len(r) > 4 and r[4]:
            continue  # conditional (language or context), not used
        code = int(r[0], 16)
        lowered = [int(x, 16) for x in r[1].split()]
        uppered = [int(x, 16) for x in r[3].split()]
        if len(lowered) != 1 or lowered[0] != code:
            full_lower.append((code, lowered))
        if len(uppered) > 1:
            full_upper.append((code, uppered))
    full_fold = [(int(r[0], 16), [int(x, 16) for x in r[2].split()])
                 for r in records(path("CaseFolding.txt")) if r[1] == "F"]

    nongraphic = predicate_ranges(
        lambda c: c != 0x20 and unicodedata.category(chr(c)) in NONGRAPHIC)
    parts = [
        "/* Generated by tools/gen_unicode.py from the Unicode Character",
        " * Database %s (the host Python's unicodedata version). Do not edit." % VERSION,
        " * Range tables are sorted inclusive [low, high]; map tables are sorted",
        " * by source code point. */",
        "",
        "#define NPL_UNICODE_VERSION \"%s\"" % VERSION,
        "",
        "/* write escapes these (Guile's non-graphic set). */",
        range_table("npl_nongraphic_ranges", nongraphic),
        "",
        range_table("npl_alphabetic_ranges",
                    property_ranges(path("DerivedCoreProperties.txt"), "Alphabetic")),
        "",
        range_table("npl_uppercase_ranges",
                    property_ranges(path("DerivedCoreProperties.txt"), "Uppercase")),
        "",
        range_table("npl_lowercase_ranges",
                    property_ranges(path("DerivedCoreProperties.txt"), "Lowercase")),
        "",
        range_table("npl_whitespace_ranges",
                    property_ranges(path("PropList.txt"), "White_Space")),
        "",
        "/* Final_Sigma context (Unicode 3.13) for string-downcase. */",
        range_table("npl_cased_ranges",
                    property_ranges(path("DerivedCoreProperties.txt"), "Cased")),
        "",
        range_table("npl_case_ignorable_ranges",
                    property_ranges(path("DerivedCoreProperties.txt"),
                                    "Case_Ignorable")),
        "",
        "/* Numeric_Type=Decimal (general category Nd): code point, digit. */",
        pair_table("npl_decimal_digits", decimal),
        "",
        "/* Simple case mappings (UnicodeData) and simple folding (C+S). */",
        pair_table("npl_simple_upper", upper),
        "",
        pair_table("npl_simple_lower", lower),
        "",
        pair_table("npl_simple_fold", fold),
        "",
        "/* Full mappings that differ from the simple ones (unconditional). */",
        full_table("npl_full_upper", full_upper),
        "",
        full_table("npl_full_lower", full_lower),
        "",
        full_table("npl_full_fold", full_fold),
        "",
    ]
    with open(os.path.join(ROOT, "runtime", "unicode_tables.h"), "w") as out:
        out.write("\n".join(parts))


if __name__ == "__main__":
    main()
