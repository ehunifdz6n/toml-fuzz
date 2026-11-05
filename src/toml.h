/* toml.h - allocation-free parser for the config subset used by toml-fuzz.
 *
 * The parser never allocates: every value lives inside struct toml_doc, so the
 * only inputs the fuzzer can exercise are the ones the parser bounds-checks by
 * hand. See README.md for the threat model.
 */
#ifndef TOML_H
#define TOML_H

#include <stddef.h>

#define TOML_MAX_INPUT       (1024u * 1024u)
#define TOML_MAX_SECTIONS    32u
#define TOML_MAX_ENTRIES     256u
#define TOML_MAX_NAME        64u
#define TOML_MAX_STRING      256u
#define TOML_MAX_ARRAY       32u

enum toml_kind {
	TOML_INT,
	TOML_BOOL,
	TOML_STRING,
	TOML_ARRAY
};

struct toml_entry {
	size_t section;                 /* index into toml_doc.sections */
	char name[TOML_MAX_NAME + 1];
	enum toml_kind kind;
	int is_bool;
	long long as_int;
	size_t string_len;
	char string[TOML_MAX_STRING + 1];
	size_t array_len;
	struct {
		enum toml_kind kind;
		long long as_int;
		size_t string_len;
		char string[TOML_MAX_STRING + 1];
	} array[TOML_MAX_ARRAY];
};

struct toml_doc {
	size_t section_count;
	size_t entry_count;
	char sections[TOML_MAX_SECTIONS][TOML_MAX_NAME + 1];
	struct toml_entry entries[TOML_MAX_ENTRIES];
};

struct toml_error {
	size_t line;
	size_t column;
	const char *message;
};

/* Parses len bytes at data. On success doc is fully described by the return
 * value 0; on failure 1 is returned and err is filled in. */
int toml_parse(const char *data, size_t len, struct toml_doc *doc,
	       struct toml_error *err);

#endif /* TOML_H */
