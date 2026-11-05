# toml-fuzz

A libFuzzer harness and a hand-written parser for the slice of TOML that shows
up in service config files: sections, `key = value` assignments, integers,
booleans, quoted strings, and flat arrays. The point is not to replace a TOML
library. It is to give a parser small enough to audit a harness that exercises
real config-file attack surface: escapes, deep arrays, oversized tokens,
truncated input, and NUL-free buffers.

Everything was written from scratch for the harness, so a finding here is a
finding about this parser's own bounds checking, not about someone else's
release cycle.

## Threat model

Config files are usually attacker-adjacent: a chart value, a CI variable, or an
uploaded `.conf` can reach a parser unvalidated, often in a privileged process.

Assumed adversary capability:

* controls the whole input, including its length and termination
* may omit the trailing newline and send NUL bytes anywhere
* may exceed every documented limit and truncate mid-token

Assumed consequences we care about: out-of-bounds read or write, integer
overflow, unbounded loop, and parser output that survives the limits of the
backing buffers (e.g. a string longer than its field).

Non-goals: nested tables, inline tables, datetimes, floats, multi-line strings,
nested arrays (rejected with a dedicated error), and UTF-8 validation. Input
over 1 MiB is rejected before parsing starts.

## Mitigations

* no allocation at all: the document lives in one caller-owned `struct toml_doc`
* every read is `pos < len`; nothing relies on a NUL terminator, so a truncated
  buffer cannot run past its end
* fixed caps on line content (`64` byte names, `256` byte strings, `32` array
  elements, `256` keys, `32` sections) with errors instead of truncation
* integers accumulate with an explicit `LLONG_MAX` check
* the fuzz target re-checks the parser's own invariants (section indices in
  range, string and array lengths inside their fields) and aborts on violation

## Build and run

Requires a C11 compiler. `clang` is required only for the coverage-guided
target, since libFuzzer ships with the sanitizer runtime.

```sh
make                      # build/repro, ASan + UBSan
make smoke                # parse examples/service.conf
./build/repro my.conf     # parse a file, print the document
```

Fuzzing:

```sh
make fuzz                 # regenerate seeds, then 60s warm-up
make fuzz FUZZ_ARGS=-max_total_time=600   # longer run
```

`tools/gen_seeds.py` writes `fuzz/corpus/` (git-ignored) from a fixed list of
grammar decisions and limit boundaries, so the starting corpus is reproducible
and does not have to be committed file by file.

## Reproducing a finding

A crash is only worth filing with a minimal input, so libFuzzer's artifact is
fed back through the non-fuzzing driver first:

```sh
./build/repro crash-3f1c2a9d
# parser/example.conf:7:19: trailing characters after value
```

Then minimise and confirm the same artefact reproduces under ASan:

```sh
./build/fuzz_parse crash-3f1c2a9d -minimize_crash=1 -runs=10000
```

Please attach the input, the sanitizer report, and the `repro` output to the
issue. A fix without a reproducer is treated as unverified.

## Layout

```
src/toml.h         public types, limits, and the parse contract
src/toml.c         parser: cursor, scanner, value readers, document writer
fuzz/fuzz_parse.c  libFuzzer entry point plus post-parse invariants
fuzz/config.dict   libFuzzer dictionary for grammar tokens and escapes
tools/gen_seeds.py deterministic corpus generator for `make fuzz`
tools/repro.c      single-file driver used to confirm and inspect inputs
examples/          sample config used by `make smoke`
```

## Status

The grammar above is enforced strictly; anything outside it is a parse error
with file position. Error messages name the failing construct, never the input
bytes, so reports stay short. Contributions of corpus seeds and dictionary
tokens are the most useful thing you can send after a bug report.
