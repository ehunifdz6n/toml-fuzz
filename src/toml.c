/* toml.c - implementation of the config subset parser.
 *
 * Grammar (deliberately small, see README):
 *   document := line*
 *   line     := blank | comment | section | assignment
 *   section  := '[' name ']' line_end
 *   assignment := name '=' value line_end
 *   value    := int | bool | string | array
 *
 * Every read goes through c->pos < c->len; nothing relies on a NUL sentinel.
 */
#include "toml.h"

#include <limits.h>
#include <string.h>

struct cursor {
	const char *data;
	size_t len;
	size_t pos;
	size_t line;
	size_t line_start;
};

static int fail(struct toml_error *err, const struct cursor *c, const char *msg)
{
	if (err != NULL) {
		err->line = c->line;
		err->column = c->pos - c->line_start + 1;
		err->message = msg;
	}
	return -1;
}

static int at_end(const struct cursor *c)
{
	return c->pos >= c->len;
}

static int peek(const struct cursor *c)
{
	return at_end(c) ? -1 : (unsigned char)c->data[c->pos];
}

static void advance(struct cursor *c)
{
	if (at_end(c))
		return;
	if (c->data[c->pos] == '\n') {
		c->line++;
		c->pos++;
		c->line_start = c->pos;
		return;
	}
	c->pos++;
}

static int is_ws(int ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r';
}

static int is_name_char(int ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
	       (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
}

static void skip_ws(struct cursor *c)
{
	while (is_ws(peek(c)))
		advance(c);
}

static void skip_comment(struct cursor *c)
{
	if (peek(c) != '#')
		return;
	while (!at_end(c) && peek(c) != '\n')
		advance(c);
}

/* Consumes padding plus an optional comment, then requires a line boundary. */
static int line_end(struct cursor *c, struct toml_error *err)
{
	skip_ws(c);
	skip_comment(c);
	if (at_end(c))
		return 0;
	if (peek(c) != '\n')
		return fail(err, c, "trailing characters after value");
	advance(c);
	return 0;
}

static int read_name(struct cursor *c, struct toml_error *err, char *out)
{
	size_t n = 0;

	while (is_name_char(peek(c))) {
		if (n == TOML_MAX_NAME)
			return fail(err, c, "name longer than 64 bytes");
		out[n++] = (char)c->data[c->pos];
		advance(c);
	}
	if (n == 0)
		return fail(err, c, "empty name");
	out[n] = '\0';
	return 0;
}

static int read_string(struct cursor *c, struct toml_error *err, char *out,
		       size_t *out_len)
{
	size_t n = 0;
	int ch;

	advance(c); /* opening quote */
	for (;;) {
		ch = peek(c);
		if (ch == -1)
			return fail(err, c, "unterminated string");
		if (ch == '"') {
			advance(c);
			break;
		}
		if (ch == '\n')
			return fail(err, c, "newline inside string");
		if (ch == '\\') {
			advance(c);
			switch (peek(c)) {
			case '"': ch = '"'; break;
			case '\\': ch = '\\'; break;
			case 'n': ch = '\n'; break;
			case 't': ch = '\t'; break;
			default: return fail(err, c, "unknown escape");
			}
			advance(c);
		} else {
			advance(c);
		}
		if (n == TOML_MAX_STRING)
			return fail(err, c, "string longer than 256 bytes");
		out[n++] = (char)ch;
	}
	out[n] = '\0';
	*out_len = n;
	return 0;
}

static int read_int(struct cursor *c, struct toml_error *err, long long *out)
{
	long long value = 0;
	int neg = 0;

	if (peek(c) == '+' || peek(c) == '-') {
		neg = peek(c) == '-';
		advance(c);
	}
	if (peek(c) < '0' || peek(c) > '9')
		return fail(err, c, "expected digits");
	while (peek(c) >= '0' && peek(c) <= '9') {
		int digit = peek(c) - '0';
		if (value > (LLONG_MAX - digit) / 10)
			return fail(err, c, "integer overflow");
		value = value * 10 + digit;
		advance(c);
	}
	*out = neg ? -value : value;
	return 0;
}

static int read_keyword(struct cursor *c, struct toml_error *err,
			const char *word)
{
	for (; *word != '\0'; word++) {
		if (peek(c) != (unsigned char)*word)
			return fail(err, c, "bad keyword");
		advance(c);
	}
	return 0;
}

static int read_array(struct cursor *c, struct toml_error *err,
		      struct toml_entry *entry)
{
	advance(c); /* '[' */
	for (;;) {
		int ch;

		skip_ws(c);
		while (peek(c) == '\n') {
			advance(c);
			skip_ws(c);
		}
		if (peek(c) == ']') {
			advance(c);
			return 0;
		}
		if (entry->array_len == TOML_MAX_ARRAY)
			return fail(err, c, "array longer than 32 elements");
		if (peek(c) == '[')
			return fail(err, c, "nested arrays unsupported");
		if (peek(c) == '"') {
			entry->array[entry->array_len].kind = TOML_STRING;
			if (read_string(c, err,
					entry->array[entry->array_len].string,
					&entry->array[entry->array_len]
						 .string_len) != 0)
				return -1;
		} else {
			entry->array[entry->array_len].kind = TOML_INT;
			if (read_int(c, err,
				     &entry->array[entry->array_len].as_int) != 0)
				return -1;
		}
		entry->array_len++;
		skip_ws(c);
		while (peek(c) == '\n') {
			advance(c);
			skip_ws(c);
		}
		ch = peek(c);
		if (ch == ',') {
			advance(c);
			continue;
		}
		if (ch == ']') {
			advance(c);
			return 0;
		}
		return fail(err, c, "expected ',' or ']'");
	}
}

static int read_value(struct cursor *c, struct toml_error *err,
		      struct toml_entry *entry)
{
	int ch = peek(c);

	if (ch == '"') {
		entry->kind = TOML_STRING;
		return read_string(c, err, entry->string, &entry->string_len);
	}
	if (ch == '[') {
		entry->kind = TOML_ARRAY;
		return read_array(c, err, entry);
	}
	if (ch == 't' || ch == 'f') {
		entry->kind = TOML_BOOL;
		entry->is_bool = ch == 't';
		return read_keyword(c, err, ch == 't' ? "true" : "false");
	}
	if (ch == '+' || ch == '-' || (ch >= '0' && ch <= '9')) {
		entry->kind = TOML_INT;
		return read_int(c, err, &entry->as_int);
	}
	return fail(err, c, "unsupported value");
}

static int duplicate_key(const struct toml_doc *doc, size_t section,
			 const char *name)
{
	size_t i;

	for (i = 0; i < doc->entry_count; i++) {
		if (doc->entries[i].section == section &&
		    doc->entries[i].name[0] == name[0] &&
		    strcmp(doc->entries[i].name, name) == 0)
			return 1;
	}
	return 0;
}

static int parse_line(struct cursor *c, struct toml_doc *doc,
		      struct toml_error *err, size_t *section)
{
	struct toml_entry *entry;

	if (peek(c) == '[') {
		advance(c);
		if (doc->section_count == TOML_MAX_SECTIONS)
			return fail(err, c, "more than 32 sections");
		if (read_name(c, err, doc->sections[doc->section_count]) != 0)
			return -1;
		if (peek(c) != ']')
			return fail(err, c, "expected ']'");
		advance(c);
		*section = doc->section_count++;
		return line_end(c, err);
	}

	if (doc->entry_count == TOML_MAX_ENTRIES)
		return fail(err, c, "more than 256 keys");
	entry = &doc->entries[doc->entry_count];
	entry->section = *section;
	if (read_name(c, err, entry->name) != 0)
		return -1;
	if (duplicate_key(doc, *section, entry->name))
		return fail(err, c, "duplicate key");
	skip_ws(c);
	if (peek(c) != '=')
		return fail(err, c, "expected '='");
	advance(c);
	skip_ws(c);
	if (read_value(c, err, entry) != 0)
		return -1;
	doc->entry_count++;
	return line_end(c, err);
}

int toml_parse(const char *data, size_t len, struct toml_doc *doc,
	       struct toml_error *err)
{
	struct cursor c = { data, len, 0, 1, 0 };
	size_t section = 0;
	int rc = 0;

	if (doc == NULL || (data == NULL && len != 0))
		return fail(err, &c, "invalid argument");
	doc->section_count = 1;
	doc->entry_count = 0;
	doc->sections[0][0] = '\0';
	if (len > TOML_MAX_INPUT)
		return fail(err, &c, "input larger than 1 MiB");

	while (!at_end(&c)) {
		skip_ws(&c);
		skip_comment(&c);
		if (at_end(&c))
			break;
		if (peek(&c) == '\n') {
			advance(&c);
			continue;
		}
		rc = parse_line(&c, doc, err, &section);
		if (rc != 0)
			return rc;
	}
	return 0;
}
