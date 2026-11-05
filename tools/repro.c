/* repro.c - run the parser over a file and dump the resulting document.
 *
 * exit 0: parsed; exit 1: parse error (message on stderr); exit 2: I/O error.
 * Used to shrink and confirm crashes reported by the fuzz target.
 */
#include "../src/toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct toml_doc doc;

static int read_file(const char *path, char **out, size_t *out_len)
{
	FILE *fp = fopen(path, "rb");
	long size;

	if (fp == NULL)
		return -1;
	if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0) {
		fclose(fp);
		return -1;
	}
	rewind(fp);
	*out = malloc((size_t)size + 1);
	if (*out == NULL) {
		fclose(fp);
		return -1;
	}
	*out_len = fread(*out, 1, (size_t)size, fp);
	(*out)[*out_len] = '\0';
	fclose(fp);
	return 0;
}

static void print_value(const struct toml_entry *entry)
{
	size_t i;

	switch (entry->kind) {
	case TOML_INT:
		printf("%lld\n", entry->as_int);
		break;
	case TOML_BOOL:
		printf("%s\n", entry->is_bool ? "true" : "false");
		break;
	case TOML_STRING:
		printf("\"%s\" (%zu bytes)\n", entry->string, entry->string_len);
		break;
	case TOML_ARRAY:
		printf("[%zu elements]\n", entry->array_len);
		for (i = 0; i < entry->array_len; i++) {
			if (entry->array[i].kind == TOML_STRING)
				printf("  \"%s\"\n", entry->array[i].string);
			else
				printf("  %lld\n", entry->array[i].as_int);
		}
		break;
	}
}

int main(int argc, char **argv)
{
	struct toml_error err;
	char *data = NULL;
	size_t len = 0;
	size_t i;

	if (argc != 2) {
		fprintf(stderr, "usage: %s FILE\n", argv[0]);
		return 2;
	}
	if (read_file(argv[1], &data, &len) != 0) {
		perror(argv[1]);
		return 2;
	}
	if (toml_parse(data, len, &doc, &err) != 0) {
		fprintf(stderr, "%s:%zu:%zu: %s\n", argv[1], err.line,
			err.column, err.message);
		free(data);
		return 1;
	}
	for (i = 0; i < doc.entry_count; i++) {
		printf("[%s] %s = ", doc.sections[doc.entries[i].section],
		       doc.entries[i].name);
		print_value(&doc.entries[i]);
	}
	free(data);
	return 0;
}
