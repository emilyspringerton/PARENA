/* tools/hoard/hoard_host.c -- the thin C host behind tools/hoard/hoard.prn (see hoard_host.h for what belongs here
 * and why). */
#include "hoard_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static Arena g_arena;
static Arena g_state;
static Arena g_request;

Arena *hd_state_arena(void) { return &g_state; }
Arena *hd_request_arena(void) { return &g_request; }

int hd_request_reset(void) {
    arena_free_all(&g_request);
    arena_init(&g_request);
    return 0;
}

static char *dup_in(const char *s, size_t n, Arena *dest) {
    char *out = (char *)arena_alloc(dest, n + 1);
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

char *hd_env(const char *key, const char *fallback, Arena *dest) {
    const char *v = key ? getenv(key) : NULL;
    if (!v || !*v) v = fallback ? fallback : "";
    {
        size_t n = strlen(v);
        /* paths are joined with "/" in hoard.prn, so a trailing separator goes first ("C:\" stays whole) */
        while (n > 1 && (v[n - 1] == '/' || v[n - 1] == '\\') && !(n == 3 && v[1] == ':')) n--;
        return dup_in(v, n, dest);
    }
}

char *hd_read_file(const char *path, Arena *dest) {
    FILE *f = fopen(path, "rb");
    long n;
    size_t got;
    char *buf;
    if (!f) return dup_in("", 0, dest);
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) { fclose(f); return dup_in("", 0, dest); }
    rewind(f);
    buf = (char *)arena_alloc(dest, (size_t)n + 1);
    got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

/* Calls `each` for every Account/<name>/SavedVariables/<addon>.lua under wtf that exists. */
typedef void (*sv_each_fn)(const char *account, const char *path, long mtime, void *ctx);

static void sv_check(const char *wtf, const char *account, const char *addon, sv_each_fn each, void *ctx) {
    char path[2048];
    struct stat st;
    if (strcmp(account, ".") == 0 || strcmp(account, "..") == 0) return;
    snprintf(path, sizeof path, "%s/Account/%s/SavedVariables/%s.lua", wtf, account, addon);
    if (stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG) each(account, path, (long)st.st_mtime, ctx);
}

static void sv_walk(const char *wtf, const char *addon, sv_each_fn each, void *ctx) {
#ifdef _WIN32
    char pattern[2048];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pattern, sizeof pattern, "%s/Account/*", wtf);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) sv_check(wtf, fd.cFileName, addon, each, ctx);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    char dir[2048];
    DIR *d;
    struct dirent *e;
    snprintf(dir, sizeof dir, "%s/Account", wtf);
    d = opendir(dir);
    if (!d) return;
    while ((e = readdir(d)) != NULL) sv_check(wtf, e->d_name, addon, each, ctx);
    closedir(d);
#endif
}

typedef struct { char *buf; size_t len, cap; Arena *dest; } ListCtx;

static void list_add(const char *account, const char *path, long mtime, void *vctx) {
    ListCtx *c = (ListCtx *)vctx;
    size_t need = strlen(account) + strlen(path) + 3;
    (void)mtime;
    if (c->len + need + 1 > c->cap) {
        size_t cap = c->cap * 2 + need;
        char *nb = (char *)arena_alloc(c->dest, cap);
        memcpy(nb, c->buf, c->len);
        c->buf = nb;
        c->cap = cap;
    }
    c->len += (size_t)sprintf(c->buf + c->len, "%s\t%s\n", account, path);
}

char *hd_sv_list(const char *wtf, const char *addon, Arena *dest) {
    ListCtx c;
    c.cap = 1024;
    c.len = 0;
    c.dest = dest;
    c.buf = (char *)arena_alloc(dest, c.cap);
    c.buf[0] = 0;
    sv_walk(wtf, addon, list_add, &c);
    c.buf[c.len] = 0;
    return c.buf;
}

static void newest(const char *account, const char *path, long mtime, void *vctx) {
    long *best = (long *)vctx;
    (void)account;
    (void)path;
    if (mtime > *best) *best = mtime;
}

int hd_sv_saved_at(const char *wtf, const char *addon) {
    long best = 0;
    sv_walk(wtf, addon, newest, &best);
    return (int)best;
}

/* Windows runs popen's command through `cmd /c`, which strips the first and last quote when the line starts with
 * one; wrapping the whole line in one more pair keeps every quoted path intact. POSIX /bin/sh needs nothing. */
char *hd_shell_wrap(const char *cmd, Arena *dest) {
#ifdef _WIN32
    size_t n = strlen(cmd);
    char *out = (char *)arena_alloc(dest, n + 3);
    out[0] = '"';
    memcpy(out + 1, cmd, n);
    out[n + 1] = '"';
    out[n + 2] = 0;
    return out;
#else
    return dup_in(cmd, strlen(cmd), dest);
#endif
}

int main(void) {
    /* line-buffered: a server never exits to flush, and its log lines are how it says what went wrong */
    setvbuf(stdout, NULL, _IOLBF, 0);
    arena_init(&g_arena);
    arena_init(&g_state);
    arena_init(&g_request);
    return hoard_run(&g_arena);
}
