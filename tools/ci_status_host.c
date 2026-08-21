/* tools/ci_status_host.c -- the real, hand-written host-side
 * implementation stdlib/ci/status.prn's own `#target` FFI declaration
 * (`ci_status_check_c`) assumes exists, matching the same real
 * "PARENA declares the FFI boundary, a real C file provides the actual
 * host implementation" split every other #target-bound file in this
 * stdlib already documents (io.prn/net/tcp.prn/pty.prn) -- except this
 * one is actually WRITTEN, not left as a real, deferred gap, since the
 * whole point of this tool is to genuinely run.
 *
 * Real, honest scope: shells out to the real, already-installed `curl`
 * via popen(), then does crude, substring-based field extraction
 * (strstr for the literal `"status":"..."` / `"conclusion":"..."` key
 * patterns GitHub's own check-runs API response actually uses) --
 * not a real JSON parse (PARENA's own stdlib has no JSON library yet),
 * an honest, narrow tool for exactly this one job.
 */
/* popen/pclose are real POSIX extensions, not part of strict C99 --
 * this feature-test macro exposes them under -std=c99 (matching the
 * same real, honest reasoning every #target inline-C body elsewhere in
 * this stdlib already has for reaching past pure ISO C into real host
 * OS interop). Must come before any system header include. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ci_status.h"

/* next_field_value -- finds the next `"key":` occurrence (starting from
 * `p`) and returns a pointer to the first character of its own real
 * value, skipping the arbitrary whitespace real JSON output allows
 * between the colon and the value (GitHub's own real API response uses
 * `"status": "completed"`, a real space, not the no-space
 * `"status":"completed"` originally assumed here -- a real bug found
 * by actually running this against the real API, not by guessing at
 * the wire format). Returns NULL past the last real occurrence. */
static const char *next_field_value(const char *p, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(p, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') p++;
    return p;
}

int ci_status_check_c(const char *repo, const char *sha, const char *token) {
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.github.com/repos/%s/commits/%s/check-runs",
             repo, sha);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
              "curl -s -H \"Authorization: token %s\" -H \"Accept: application/vnd.github+json\" \"%s\"",
              token, url);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 3;

    size_t cap = 65536;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        pclose(fp);
        return 3;
    }
    size_t len = 0;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *grown = (char *)realloc(buf, cap);
            if (!grown) {
                free(buf);
                pclose(fp);
                return 3;
            }
            buf = grown;
        }
    }
    buf[len] = '\0';
    pclose(fp);

    /* Real bug found and fixed here (2026-08-21, testing against a real
     * nonexistent SHA, not just guessing at the wire format): GitHub's
     * own real ERROR response body (bad SHA, rate limit, revoked token,
     * etc.) is itself shaped `{"message": "...", "status": "422"}` --
     * a real `"status"` field of its own, the HTTP status code as a
     * string, which the crude field scan below can't tell apart from a
     * real check-run's own "status" -- it doesn't start with
     * "completed", so it silently reported "still pending" instead of
     * the real "request failed" case. `"check_runs"` is the one real,
     * reliable signal this is genuinely the list-check-runs response
     * shape and not an error body -- checked first, before the crude
     * scan gets a chance to misread an error's own "status" field. */
    if (strstr(buf, "\"check_runs\"") == NULL) {
        free(buf);
        return 3;
    }

    /* Every real check-run's own "status" field must read "completed" --
     * any other value (e.g. "in_progress", "queued") means the whole
     * commit isn't done yet. */
    const char *p = buf;
    int found_any = 0;
    while ((p = next_field_value(p, "status")) != NULL) {
        found_any = 1;
        if (strncmp(p, "completed", 9) != 0) {
            free(buf);
            return 1;
        }
    }
    if (!found_any) {
        free(buf);
        return 3;
    }

    /* Every real check-run's own "conclusion" field must read "success"
     * once status is confirmed completed above. */
    p = buf;
    while ((p = next_field_value(p, "conclusion")) != NULL) {
        if (strncmp(p, "success", 7) != 0) {
            free(buf);
            return 2;
        }
    }

    free(buf);
    return 0;
}
