/* i18n.h -- the translation core module (Collection of Value, Step 0).
 *
 * User-facing text is written as a `tr` form:
 *
 *     (tr "Hello, traveller")                              ; id derived from the text
 *     (tr "Welcome back" :id "login.welcome")              ; explicit, stable id
 *     (tr "Hello, {name}!" :id "greet" :args [name])       ; placeholders, declared
 *
 * The default string is inline, so a program with no catalogue reads exactly as it did before.
 * `tr` is a SOURCE-LEVEL pass: i18n_lower() runs after parsing and before region analysis, rewriting
 * every `tr` form into a plain string literal. No backend (C, TS, Java, LLVM, Lua) ever sees a `tr`,
 * and the module costs nothing at runtime -- the choice of language is made at build time.
 *
 *   - no catalogue given  -> every `tr` becomes its inline default ("module off")
 *   - catalogue given     -> every `tr` becomes the catalogue's text for its id, falling back to the
 *                            default when the id is absent ("module on"); the count of fallbacks is
 *                            reported so a build can warn
 *
 * Catalogues are ordinary PARENA data, read by the existing parser, so no second file format and no
 * second escape syntax exists:
 *
 *     (catalog :locale "deDE"
 *       ["greet" "Hallo, {name}!"]
 *       ["login.welcome" "Willkommen zurück"])
 *
 * i18n_extract() writes the enUS catalogue from a program's own `tr` forms; i18n_check() is the
 * missing-string check CI runs: every one of the 12 WoW client locales must have a catalogue, every id
 * in the source must be present in it, no catalogue may carry an id the source no longer has, and a
 * translation must use exactly the placeholders its default declares.
 *
 * NOT done in v0, named: a `tr` that carries placeholders cannot be lowered yet (the emitters have no
 * string formatting form to lower it to), so i18n_lower() refuses it BY NAME rather than emitting a
 * template with literal `{name}` in it. Extraction and checking already understand placeholders.
 */
#ifndef PARENA_I18N_H
#define PARENA_I18N_H

#include "arena.h"
#include "ast.h"
#include <stddef.h>

#define I18N_LOCALE_COUNT 12

/* WoW's 12 client locales, in the order the spec lists them. */
extern const char *const I18N_LOCALES[I18N_LOCALE_COUNT];

typedef struct {
    const char *id;
    const char *text;
} I18nEntry;

typedef struct {
    const char *locale;
    I18nEntry *entries;
    size_t count;
} I18nCatalog;

/* The id a `tr` without :id gets: "s_" + 16 hex digits of FNV-1a 64 over the default text. */
const char *i18n_derive_id(Arena *arena, const char *text, size_t len);

/* Parse a catalogue file's text. Returns NULL on success, else an arena-owned error message. */
const char *i18n_load_catalog(Arena *arena, const char *src, size_t len, I18nCatalog *out);

/* Rewrite every `tr` form in `program` to a string literal, in place. `cat` may be NULL (module off).
 * *fallbacks (optional) is set to how many ids the catalogue lacked. NULL on success, else an error. */
const char *i18n_lower(Arena *arena, Node *program, const I18nCatalog *cat, size_t *fallbacks);

/* Write the enUS catalogue for `program`'s `tr` forms (deduplicated by id) as catalogue text.
 * NULL on success; an id used with two different defaults is an error. */
const char *i18n_extract(Arena *arena, Node *program, const char **out_text);

/* The missing-string check. *report is a newline-separated list of problems ("" when clean) and
 * *problems their count. The function itself returns non-NULL only for a malformed `tr` in the source. */
const char *i18n_check(Arena *arena, Node *program, const I18nCatalog *cats, size_t ncats,
                       const char **report, size_t *problems);

#endif /* PARENA_I18N_H */
