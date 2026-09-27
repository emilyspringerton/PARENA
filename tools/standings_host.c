/* tools/standings_host.c -- the hand-written host-side implementation
 * stdlib/league/standings.prn's own `#target` declaration
 * (`standings_print_c`) calls. Same "PARENA declares the FFI boundary, a
 * real C file provides the host implementation" split as
 * tools/ci_status_host.c, which this file deliberately mirrors.
 *
 * Real, honest scope: runs the already-installed `curl` via popen() against
 * IDUNA's public `GET /api/v1/game-checkpoints/<game>` (metadata only, no
 * weights, no auth), then walks the response -- one flat JSON array of flat
 * checkpoint objects (IDUNA/internal/brawlpit/checkpoint_store.go's own
 * `Checkpoint` struct) -- with a small, narrow scanner. Unlike ci_status's
 * strstr field grep, the scanner does track string/nesting boundaries, so a
 * checkpoint `name` that happens to contain `"role":` can't be misread as a
 * real field. It is still NOT a general JSON library: it only understands
 * this one response shape and skips anything else it sees.
 *
 * Shell safety: `game` and `base_url` are validated against a strict
 * character allowlist BEFORE any command line is built (process.prn's own
 * run-capture header already names the `/bin/sh -c` injection risk this
 * avoids), then single-quoted.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "standings.h"

#define ST_FIELD_MAX 128

typedef struct {
    long long id;
    char name[ST_FIELD_MAX];
    char role[ST_FIELD_MAX];
    char source[ST_FIELD_MAX];
    long long generation;
    double elo;
} StEntry;

static int valid_game(const char *g) {
    size_t n = strlen(g);
    if (n == 0 || n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = g[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/* Scheme + host[:port] + path only: no query string, no quotes, no shell
 * metacharacters, no whitespace. */
static int valid_base_url(const char *u) {
    size_t n = strlen(u);
    if (n == 0 || n > 400) return 0;
    if (strncmp(u, "http://", 7) != 0 && strncmp(u, "https://", 8) != 0) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = u[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == ':' || c == '/' || c == '_' || c == '-' || c == '~')) {
            return 0;
        }
    }
    return 1;
}

static char *fetch(const char *cmd, int *status) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    size_t cap = 65536, len = 0, n;
    char *buf = (char *)malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *grown = (char *)realloc(buf, cap);
            if (!grown) { free(buf); pclose(fp); return NULL; }
            buf = grown;
        }
    }
    buf[len] = '\0';
    *status = pclose(fp);
    return buf;
}

/* ---- the narrow scanner ------------------------------------------------ */

static const char *ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Reads a JSON string starting at `p` (which must point at '"'), decoding
 * the simple escapes into `out` (truncated to `cap`; \uXXXX becomes '?').
 * Returns the position just past the closing quote, or NULL. */
static const char *read_str(const char *p, char *out, size_t cap) {
    if (*p != '"') return NULL;
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            if (!e) return NULL;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u':
                    for (int i = 0; i < 4; i++) { if (!*p) return NULL; p++; }
                    c = '?';
                    break;
                default: c = e; break; /* \" \\ \/ */
            }
        }
        if (out && o + 1 < cap) out[o++] = c;
    }
    if (*p != '"') return NULL;
    if (out && cap) out[o] = '\0';
    return p + 1;
}

/* Skips any one JSON value (string, number, literal, nested object/array). */
static const char *skip_value(const char *p) {
    p = ws(p);
    if (*p == '"') return read_str(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = read_str(p, NULL, 0);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') {
                depth--;
                if (depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

/* Parses one flat checkpoint object at `p` (pointing at '{') into `e`.
 * Returns the position just past its closing '}', or NULL on malformed input. */
static const char *read_entry(const char *p, StEntry *e) {
    memset(e, 0, sizeof(*e));
    p = ws(p + 1);
    if (*p == '}') return p + 1;
    for (;;) {
        char key[64];
        p = read_str(ws(p), key, sizeof(key));
        if (!p) return NULL;
        p = ws(p);
        if (*p != ':') return NULL;
        p = ws(p + 1);
        if (strcmp(key, "id") == 0) {
            e->id = strtoll(p, NULL, 10);
            p = skip_value(p);
        } else if (strcmp(key, "generation") == 0) {
            e->generation = strtoll(p, NULL, 10);
            p = skip_value(p);
        } else if (strcmp(key, "elo") == 0) {
            e->elo = strtod(p, NULL);
            p = skip_value(p);
        } else if (strcmp(key, "name") == 0 && *p == '"') {
            p = read_str(p, e->name, sizeof(e->name));
        } else if (strcmp(key, "role") == 0 && *p == '"') {
            p = read_str(p, e->role, sizeof(e->role));
        } else if (strcmp(key, "source_location") == 0 && *p == '"') {
            p = read_str(p, e->source, sizeof(e->source));
        } else {
            p = skip_value(p);
        }
        if (!p) return NULL;
        p = ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') return p + 1;
        return NULL;
    }
}

/* Newest generation first; ties broken by newest id. */
static int cmp_entry(const void *a, const void *b) {
    const StEntry *x = (const StEntry *)a, *y = (const StEntry *)b;
    if (x->generation != y->generation) return x->generation < y->generation ? 1 : -1;
    if (x->id != y->id) return x->id < y->id ? 1 : -1;
    return 0;
}

static void print_role(StEntry *all, size_t count, const char *role, int top) {
    int shown = 0;
    for (size_t i = 0; i < count && shown < top; i++) {
        if (strcmp(all[i].role, role) != 0) continue;
        printf("  id=%4lld  %-18s gen=%3lld  elo=%7.1f  %s%s%s%s\n",
               all[i].id, all[i].role, all[i].generation, all[i].elo, all[i].name,
               all[i].source[0] ? "  [" : "", all[i].source, all[i].source[0] ? "]" : "");
        shown++;
    }
}

int standings_print_c(const char *base_url, const char *game, int top) {
    if (!valid_game(game)) {
        fprintf(stderr, "standings: invalid game name (allowed: a-z 0-9 _ -, 1..64 chars)\n");
        return 4;
    }
    if (!valid_base_url(base_url)) {
        fprintf(stderr, "standings: invalid base URL (http(s)://host[:port][/path], no query string or shell characters)\n");
        return 4;
    }
    if (top < 1) top = 1;

    size_t blen = strlen(base_url);
    while (blen > 0 && base_url[blen - 1] == '/') blen--;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "curl -sS -f --max-time 20 '%.*s/api/v1/game-checkpoints/%s'",
             (int)blen, base_url, game);

    int status = 0;
    char *buf = fetch(cmd, &status);
    if (!buf) return 3;
    if (status != 0) {
        free(buf);
        return 3;
    }

    const char *p = ws(buf);
    if (*p != '[') {
        free(buf);
        return 3;
    }
    p = ws(p + 1);

    size_t cap = 64, count = 0;
    StEntry *all = (StEntry *)malloc(cap * sizeof(StEntry));
    if (!all) { free(buf); return 3; }
    while (*p && *p != ']') {
        if (*p != '{') { free(all); free(buf); return 3; }
        if (count == cap) {
            cap *= 2;
            StEntry *grown = (StEntry *)realloc(all, cap * sizeof(StEntry));
            if (!grown) { free(all); free(buf); return 3; }
            all = grown;
        }
        p = read_entry(p, &all[count]);
        if (!p) { free(all); free(buf); return 3; }
        count++;
        p = ws(p);
        if (*p == ',') p = ws(p + 1);
    }
    if (*p != ']') { free(all); free(buf); return 3; }
    free(buf);

    printf("-- league standings: %s at %.*s (%zu checkpoints, newest %d per role) --\n",
           game, (int)blen, base_url, count, top);
    if (count == 0) {
        printf("  (registry is empty: no checkpoints for this game yet)\n");
        free(all);
        return 1;
    }

    qsort(all, count, sizeof(StEntry), cmp_entry);

    /* The three PFSP league roles first, in their canonical order (BRAWLPIT's
     * rl_league.py), then any other role in order of first appearance. */
    static const char *known[] = {"main", "main_exploiter", "league_exploiter"};
    for (size_t k = 0; k < 3; k++) print_role(all, count, known[k], top);
    for (size_t i = 0; i < count; i++) {
        int is_known = 0, seen_before = 0;
        for (size_t k = 0; k < 3; k++) if (strcmp(all[i].role, known[k]) == 0) is_known = 1;
        for (size_t j = 0; j < i; j++) if (strcmp(all[j].role, all[i].role) == 0) seen_before = 1;
        if (!is_known && !seen_before) print_role(all, count, all[i].role, top);
    }

    free(all);
    return 0;
}
