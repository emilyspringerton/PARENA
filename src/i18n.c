/* i18n.c -- the translation core module. See i18n.h for the contract and the honest v0 limits. */
#include "i18n.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const I18N_LOCALES[I18N_LOCALE_COUNT] = {
    "enUS", "enGB", "deDE", "esES", "esMX", "frFR", "itIT", "ptBR", "ruRU", "koKR", "zhCN", "zhTW",
};

#define PH_MAX 16
#define PH_LEN 48

/* ---- small helpers -------------------------------------------------------------------------- */

static const char *err_done(Arena *arena, const char *buf) { return arena_strdup(arena, buf, strlen(buf)); }

/* fmt holds one %d */
static const char *errl(Arena *arena, const char *fmt, int line) {
    char buf[512];
    snprintf(buf, sizeof(buf), fmt, line);
    return err_done(arena, buf);
}

/* fmt holds one %s */
static const char *errs(Arena *arena, const char *fmt, const char *s) {
    char buf[512];
    snprintf(buf, sizeof(buf), fmt, s ? s : "");
    return err_done(arena, buf);
}

/* fmt holds a %s then a %d */
static const char *errsl(Arena *arena, const char *fmt, const char *s, int line) {
    char buf[512];
    snprintf(buf, sizeof(buf), fmt, s ? s : "", line);
    return err_done(arena, buf);
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_put(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1) cap *= 2;
        char *p = (char *)realloc(b->data, cap);
        if (!p) {
            fprintf(stderr, "parena: out of memory (i18n buffer)\n");
            exit(1);
        }
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_str(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }

/* Writes `s` as a PARENA string literal -- the inverse of the lexer's own escapes, so a catalogue
   round-trips through the one existing parser. Non-ASCII bytes pass through unchanged (UTF-8 in,
   UTF-8 out), which is what the lexer reads back. */
static void buf_quoted(Buf *b, const char *s, size_t n) {
    buf_put(b, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
            case '"': buf_put(b, "\\\"", 2); break;
            case '\\': buf_put(b, "\\\\", 2); break;
            case '\n': buf_put(b, "\\n", 2); break;
            case '\t': buf_put(b, "\\t", 2); break;
            case '\r': buf_put(b, "\\r", 2); break;
            default: buf_put(b, &s[i], 1); break;
        }
    }
    buf_put(b, "\"", 1);
}

static const char *buf_finish(Arena *arena, Buf *b) {
    const char *out = arena_strdup(arena, b->data ? b->data : "", b->len);
    free(b->data);
    b->data = NULL;
    return out;
}

static int is_sym(const Node *n, const char *text) {
    return n && n->type == NODE_SYMBOL && n->text && strcmp(n->text, text) == 0;
}

static int is_kw(const Node *n, const char *text) {
    return n && n->type == NODE_KEYWORD && n->text && strcmp(n->text, text) == 0;
}

static int is_tr_form(const Node *n) {
    return n && n->type == NODE_LIST && n->child_count >= 1 && is_sym(n->children[0], "tr");
}

/* ---- ids ------------------------------------------------------------------------------------ */

const char *i18n_derive_id(Arena *arena, const char *text, size_t len) {
    unsigned long long h = 14695981039346656037ULL; /* FNV-1a 64 offset basis */
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)text[i];
        h *= 1099511628211ULL;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "s_%016llx", h);
    return arena_strdup(arena, buf, strlen(buf));
}

/* ---- placeholders --------------------------------------------------------------------------- */

typedef struct {
    char names[PH_MAX][PH_LEN];
    size_t count;
} Placeholders;

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

/* A placeholder is "{name}" where name is a non-empty run of identifier characters. Anything else
   containing a brace is literal text. Returns 0, or -1 when the string has more than PH_MAX of them or
   one is longer than PH_LEN. Repeated uses of one name count once. */
static int scan_placeholders(const char *s, size_t n, Placeholders *out) {
    out->count = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '{') continue;
        size_t j = i + 1;
        while (j < n && is_ident_char(s[j])) j++;
        if (j == i + 1 || j >= n || s[j] != '}') continue;
        size_t len = j - i - 1;
        if (len >= PH_LEN) return -1;
        int seen = 0;
        for (size_t k = 0; k < out->count; k++) {
            if (strlen(out->names[k]) == len && memcmp(out->names[k], s + i + 1, len) == 0) seen = 1;
        }
        if (!seen) {
            if (out->count >= PH_MAX) return -1;
            memcpy(out->names[out->count], s + i + 1, len);
            out->names[out->count][len] = '\0';
            out->count++;
        }
        i = j;
    }
    return 0;
}

static int has_name(const Placeholders *p, const char *name) {
    for (size_t i = 0; i < p->count; i++) {
        if (strcmp(p->names[i], name) == 0) return 1;
    }
    return 0;
}

static int same_placeholders(const Placeholders *a, const Placeholders *b) {
    if (a->count != b->count) return 0;
    for (size_t i = 0; i < a->count; i++) {
        if (!has_name(b, a->names[i])) return 0;
    }
    return 1;
}

/* ---- parsing one tr form -------------------------------------------------------------------- */

typedef struct {
    const char *id;
    const char *text;
    size_t text_len;
    Placeholders ph;
    int line;
} Tr;

static const char *parse_tr(Arena *arena, const Node *form, Tr *out) {
    int line = form->line;
    if (form->child_count < 2 || form->children[1]->type != NODE_STRING) {
        return errl(arena, "tr: the first argument must be a string literal holding the default text (line %d)", line);
    }
    const Node *text = form->children[1];
    out->text = text->text;
    out->text_len = text->text_len;
    out->line = line;
    out->id = NULL;
    Placeholders declared;
    declared.count = 0;
    int has_args = 0;

    size_t i = 2;
    while (i < form->child_count) {
        const Node *key = form->children[i];
        if (i + 1 >= form->child_count) {
            return errsl(arena, "tr: option '%s' has no value (line %d)", key->text ? key->text : "?", line);
        }
        const Node *val = form->children[i + 1];
        if (is_kw(key, ":id")) {
            if (val->type != NODE_STRING || val->text_len == 0) {
                return errl(arena, "tr: :id must be a non-empty string literal (line %d)", line);
            }
            out->id = val->text;
        } else if (is_kw(key, ":args")) {
            if (val->type != NODE_VEC) {
                return errl(arena, "tr: :args must be a vector of placeholder names, e.g. [name] (line %d)", line);
            }
            has_args = 1;
            for (size_t k = 0; k < val->child_count; k++) {
                const Node *a = val->children[k];
                if (a->type != NODE_SYMBOL || a->text_len == 0 || a->text_len >= PH_LEN) {
                    return errl(arena, "tr: every :args entry must be a plain name (line %d)", line);
                }
                if (declared.count >= PH_MAX) {
                    return errl(arena, "tr: too many placeholders (line %d)", line);
                }
                memcpy(declared.names[declared.count], a->text, a->text_len + 1);
                declared.count++;
            }
        } else {
            return errsl(arena, "tr: unknown option '%s' (only :id and :args exist) (line %d)", key->text ? key->text : "?", line);
        }
        i += 2;
    }

    if (scan_placeholders(out->text, out->text_len, &out->ph) != 0) {
        return errl(arena, "tr: too many or too long placeholders in the default text (line %d)", line);
    }
    if (!has_args) declared.count = 0;
    if (!same_placeholders(&out->ph, &declared)) {
        return errl(arena, "tr: the {placeholders} in the text and the names in :args must match exactly (line %d)", line);
    }
    if (!out->id) out->id = i18n_derive_id(arena, out->text, out->text_len);
    return NULL;
}

/* ---- catalogues ----------------------------------------------------------------------------- */

const char *i18n_load_catalog(Arena *arena, const char *src, size_t len, I18nCatalog *out) {
    const char *perr = NULL;
    Node *prog = parse_program(arena, src, len, &perr);
    if (!prog) return errs(arena, "catalogue: %s", perr ? perr : "parse error");
    if (prog->child_count != 1 || prog->children[0]->type != NODE_LIST || prog->children[0]->child_count < 1 ||
        !is_sym(prog->children[0]->children[0], "catalog")) {
        return errs(arena, "catalogue: %s", "the file must hold exactly one (catalog :locale \"xxXX\" ...) form");
    }
    const Node *cat = prog->children[0];
    out->locale = NULL;
    out->entries = NULL;
    out->count = 0;
    size_t i = 1;
    if (cat->child_count < 3 || !is_kw(cat->children[1], ":locale") || cat->children[2]->type != NODE_STRING) {
        return errs(arena, "catalogue: %s", "(catalog ...) must start with :locale \"xxXX\"");
    }
    out->locale = cat->children[2]->text;
    i = 3;
    size_t n = cat->child_count - i;
    out->entries = (I18nEntry *)arena_alloc(arena, (n ? n : 1) * sizeof(I18nEntry));
    for (; i < cat->child_count; i++) {
        const Node *e = cat->children[i];
        if (e->type != NODE_VEC || e->child_count != 2 || e->children[0]->type != NODE_STRING ||
            e->children[1]->type != NODE_STRING) {
            return errl(arena, "catalogue: every entry must be [\"id\" \"text\"] (line %d)", e->line);
        }
        out->entries[out->count].id = e->children[0]->text;
        out->entries[out->count].text = e->children[1]->text;
        out->count++;
    }
    return NULL;
}

static const I18nEntry *find_entry(const I18nCatalog *cat, const char *id) {
    if (!cat) return NULL;
    for (size_t i = 0; i < cat->count; i++) {
        if (strcmp(cat->entries[i].id, id) == 0) return &cat->entries[i];
    }
    return NULL;
}

/* ---- lowering ------------------------------------------------------------------------------- */

static const char *lower_node(Arena *arena, Node *n, const I18nCatalog *cat, size_t *fallbacks) {
    if (n->type != NODE_LIST && n->type != NODE_VEC && n->type != NODE_MAP) return NULL;
    for (size_t i = 0; i < n->child_count; i++) {
        Node *c = n->children[i];
        if (is_tr_form(c)) {
            Tr tr;
            const char *err = parse_tr(arena, c, &tr);
            if (err) return err;
            if (tr.ph.count > 0) {
                return errl(arena,
                            "tr: a string with {placeholders} cannot be lowered yet -- the emitters have no "
                            "string-formatting form (line %d)",
                            tr.line);
            }
            const char *text = tr.text;
            size_t text_len = tr.text_len;
            if (cat) {
                const I18nEntry *e = find_entry(cat, tr.id);
                if (e) {
                    text = e->text;
                    text_len = strlen(e->text);
                } else if (fallbacks) {
                    (*fallbacks)++;
                }
            }
            n->children[i] = node_new_atom(arena, NODE_STRING, text, text_len, c->line);
        } else {
            const char *err = lower_node(arena, c, cat, fallbacks);
            if (err) return err;
        }
    }
    return NULL;
}

const char *i18n_lower(Arena *arena, Node *program, const I18nCatalog *cat, size_t *fallbacks) {
    if (fallbacks) *fallbacks = 0;
    return lower_node(arena, program, cat, fallbacks);
}

/* ---- collecting the source's own strings ---------------------------------------------------- */

typedef struct {
    Tr *items;
    size_t count;
    size_t cap;
} TrList;

static const char *collect(Arena *arena, const Node *n, TrList *list) {
    if (n->type != NODE_LIST && n->type != NODE_VEC && n->type != NODE_MAP) return NULL;
    if (is_tr_form(n)) {
        Tr tr;
        const char *err = parse_tr(arena, n, &tr);
        if (err) return err;
        for (size_t i = 0; i < list->count; i++) {
            if (strcmp(list->items[i].id, tr.id) == 0) {
                if (strcmp(list->items[i].text, tr.text) != 0) {
                    return errsl(arena,
                                "tr: the id '%s' is used with two different default texts (second use at line %d)",
                                tr.id, tr.line);
                }
                return NULL; /* same id, same text: one entry */
            }
        }
        if (list->count == list->cap) {
            size_t cap = list->cap ? list->cap * 2 : 16;
            Tr *grown = (Tr *)arena_alloc(arena, cap * sizeof(Tr));
            if (list->count) memcpy(grown, list->items, list->count * sizeof(Tr));
            list->items = grown;
            list->cap = cap;
        }
        list->items[list->count++] = tr;
        return NULL;
    }
    for (size_t i = 0; i < n->child_count; i++) {
        const char *err = collect(arena, n->children[i], list);
        if (err) return err;
    }
    return NULL;
}

/* ---- extraction ----------------------------------------------------------------------------- */

const char *i18n_extract(Arena *arena, Node *program, const char **out_text) {
    TrList list = {NULL, 0, 0};
    const char *err = collect(arena, program, &list);
    if (err) return err;
    Buf b = {NULL, 0, 0};
    buf_str(&b, "(catalog :locale \"enUS\"");
    for (size_t i = 0; i < list.count; i++) {
        buf_str(&b, "\n  [");
        buf_quoted(&b, list.items[i].id, strlen(list.items[i].id));
        buf_str(&b, " ");
        buf_quoted(&b, list.items[i].text, list.items[i].text_len);
        buf_str(&b, "]");
    }
    buf_str(&b, ")\n");
    *out_text = buf_finish(arena, &b);
    return NULL;
}

/* ---- the missing-string check --------------------------------------------------------------- */

static void problem(Buf *b, size_t *count, const char *locale, const char *what, const char *id) {
    buf_str(b, locale);
    buf_str(b, ": ");
    buf_str(b, what);
    if (id) {
        buf_str(b, " '");
        buf_str(b, id);
        buf_str(b, "'");
    }
    buf_str(b, "\n");
    (*count)++;
}

const char *i18n_check(Arena *arena, Node *program, const I18nCatalog *cats, size_t ncats, const char **report,
                       size_t *problems) {
    TrList list = {NULL, 0, 0};
    const char *err = collect(arena, program, &list);
    if (err) return err;
    Buf b = {NULL, 0, 0};
    size_t count = 0;

    for (int l = 0; l < I18N_LOCALE_COUNT; l++) {
        const char *loc = I18N_LOCALES[l];
        const I18nCatalog *cat = NULL;
        for (size_t c = 0; c < ncats; c++) {
            if (strcmp(cats[c].locale, loc) == 0) cat = &cats[c];
        }
        if (!cat) {
            if (strcmp(loc, "enUS") != 0) problem(&b, &count, loc, "no catalogue supplied", NULL);
            continue; /* enUS is the source's own defaults, so it needs no catalogue */
        }
        for (size_t i = 0; i < list.count; i++) {
            const I18nEntry *e = find_entry(cat, list.items[i].id);
            if (!e) {
                problem(&b, &count, loc, "missing string", list.items[i].id);
                continue;
            }
            Placeholders theirs;
            if (scan_placeholders(e->text, strlen(e->text), &theirs) != 0 ||
                !same_placeholders(&list.items[i].ph, &theirs)) {
                problem(&b, &count, loc, "placeholders differ from the default in", list.items[i].id);
            }
        }
        for (size_t i = 0; i < cat->count; i++) {
            int used = 0;
            for (size_t k = 0; k < list.count; k++) {
                if (strcmp(list.items[k].id, cat->entries[i].id) == 0) used = 1;
            }
            if (!used) problem(&b, &count, loc, "stale id (no longer in the source)", cat->entries[i].id);
            for (size_t j = 0; j < i; j++) {
                if (strcmp(cat->entries[j].id, cat->entries[i].id) == 0) {
                    problem(&b, &count, loc, "duplicate id", cat->entries[i].id);
                    break;
                }
            }
        }
    }
    for (size_t c = 0; c < ncats; c++) {
        int known = 0;
        for (int l = 0; l < I18N_LOCALE_COUNT; l++) {
            if (strcmp(cats[c].locale, I18N_LOCALES[l]) == 0) known = 1;
        }
        if (!known) problem(&b, &count, cats[c].locale, "not one of the 12 supported locales", NULL);
    }
    *report = buf_finish(arena, &b);
    *problems = count;
    return NULL;
}
