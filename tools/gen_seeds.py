#!/usr/bin/env python3
"""gen_seeds.py - deterministic corpus seeds for toml-fuzz.

Usage: python3 tools/gen_seeds.py [outdir]

Seeds are not random noise. Each one sits on a parser decision we want
libFuzzer to reach quickly: an escape table, a limit boundary one byte before
and after the cap, or a truncation point. Rerunning the script produces the
same corpus, so a seed that finds something can be regenerated from source
control instead of living only in a fuzzer workspace.
"""
import os
import sys

INT_MAX = "9223372036854775807"
INT_OVER = "9223372036854775808"

ESCAPES = ['"\\""', '"\\\\"', '"\\n"', '"\\t"', '"\\q"']


def seeds():
    yield "empty", b""
    yield "blank-lines", b"\n\n  \n\t\n"
    yield "comment-only", b"# just a comment\n"
    yield "assign-int", b"workers = 4\n"
    yield "int-max", f"workers = {INT_MAX}\n".encode()
    yield "int-overflow", f"workers = {INT_OVER}\n".encode()
    yield "int-sign-only", b"workers = -\n"
    yield "bool-false", b"debug = false\n"
    yield "bool-truncated", b"debug = tru"
    yield "bool-prefix", b"debug = trueish\n"
    yield "section", b"[server]\nlisten = \"127.0.0.1\"\n"
    yield "section-unclosed", b"[server\nlisten = 1\n"
    yield "section-empty", b"[]\n"
    yield "section-deep-name", b"[a.b.c.d]\nx = 1\n"
    yield "section-name-max", b"[" + b"s" * 64 + b"]\n"
    yield "section-name-over", b"[" + b"s" * 65 + b"]\n"
    yield "dup-key", b"a = 1\na = 2\n"
    for i, esc in enumerate(ESCAPES):
        yield f"escape-{i}", f"k = {esc}\n".encode()
    yield "string-unterminated", b"k = \"abc"
    yield "string-newline", b"k = \"abc\ndef\"\n"
    yield "string-max", b"k = \"" + b"x" * 256 + b"\"\n"
    yield "string-over", b"k = \"" + b"x" * 257 + b"\"\n"
    yield "array-mixed", b"k = [1, \"two\", 3]\n"
    yield "array-nested-rejected", b"k = [[1], [2]]\n"
    yield "array-trailing-comma", b"k = [1, 2,]\n"
    yield "array-newlines", b"k = [\n  1,\n  2,\n]\n"
    yield "array-unclosed", b"k = [1, 2\n"
    yield "array-max", b"k = [" + b"," .join([b"1"] * 32) + b"]\n"
    yield "array-over", b"k = [" + b",".join([b"1"] * 33) + b"]\n"
    yield "no-assign", b"key\n"
    yield "no-eol", b"k = 1"
    yield "trailing-junk", b"k = 1 junk\n"
    yield "nul-inside", b"k = \"a\x00b\"\n"
    yield "crlf", b"[server]\r\nport = 1\r\n"
    yield "high-bytes", b"k = \"\xff\xfe\"\n"


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "fuzz/corpus"
    os.makedirs(outdir, exist_ok=True)
    written = 0
    for name, blob in seeds():
        with open(os.path.join(outdir, name), "wb") as fp:
            fp.write(blob)
        written += 1
    print(f"wrote {written} seeds to {outdir}")


if __name__ == "__main__":
    main()
