CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
SANI     = -fsanitize=address,undefined -fno-omit-frame-pointer
FUZZ_CC  = clang
FUZZ_FLAGS = -O1 -g -Wall -Wextra -std=c11 -fsanitize=fuzzer,address,undefined

SRC = src/toml.c

all: build/repro

build:
	mkdir -p build

build/repro: $(SRC) tools/repro.c src/toml.h | build
	$(CC) $(CFLAGS) $(SANI) -o $@ $(SRC) tools/repro.c

build/fuzz_parse: $(SRC) fuzz/fuzz_parse.c src/toml.h | build
	$(FUZZ_CC) $(FUZZ_FLAGS) -o $@ $(SRC) fuzz/fuzz_parse.c

FUZZ_ARGS ?= -max_len=65536

fuzz: build/fuzz_parse
	python3 tools/gen_seeds.py fuzz/corpus
	./build/fuzz_parse fuzz/corpus -dict=fuzz/config.dict $(FUZZ_ARGS)

smoke: build/repro
	./build/repro examples/service.conf

clean:
	rm -rf build

.PHONY: all build fuzz smoke clean
