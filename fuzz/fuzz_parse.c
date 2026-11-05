/* fuzz_parse.c - libFuzzer entry point for toml_parse.
 *
 * Build: make fuzz CC=clang
 * Run  : ./build/fuzz_parse fuzz/corpus -dict=fuzz/config.dict -max_len=65536
 */
#include "../src/toml.h"

#include <stdint.h>
#include <stdlib.h>

static struct toml_doc doc;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct toml_error err;

	toml_parse((const char *)data, size, &doc, &err);

	/* Invariants the fuzzer should never be able to break. */
	if (doc.entry_count > TOML_MAX_ENTRIES)
		abort();
	if (doc.section_count > TOML_MAX_SECTIONS)
		abort();
	for (size_t i = 0; i < doc.entry_count; i++) {
		if (doc.entries[i].section >= doc.section_count)
			abort();
		if (doc.entries[i].string_len > TOML_MAX_STRING)
			abort();
		if (doc.entries[i].array_len > TOML_MAX_ARRAY)
			abort();
	}
	return 0;
}
