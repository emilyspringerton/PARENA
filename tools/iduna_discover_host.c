/* tools/iduna_discover_host.c -- `iduna-discover`: the IDUNA base URL a native tool should use, local IDUNA first.
 *
 * Founder, 2026-11-17: "we need a local iduna to test against it needs to work so that it checks to see if there is a
 * local iduna and connects to that and if not it attempts to use cloud iduna". The rules (probe order, verdicts,
 * timeouts) are PARENA: stdlib/iduna/discover.prn, the same file the WOTAN pages run as JavaScript. The probe is
 * PARENA too: stdlib/iduna/discover_native.prn over stdlib/net/http_probe.prn. This file is only the host: environment,
 * URL splitting, printing and the exit code, the same split tools/turbogrep_host.c uses. It is concatenated onto the
 * generated C (scripts/build_iduna_discover.sh), so it calls the generated functions directly.
 *
 *   export IDUNA_BASE_URL="$(iduna-discover)"
 *
 * Environment (all optional):
 *   IDUNA_LOCAL_URL         the local IDUNA. Default http://localhost:8080 (IDUNA's own IDUNA_ADDR and BASE_URL defaults).
 *   IDUNA_CLOUD_URL         the cloud IDUNA. Default https://iam.okemily.com (EMILY gitops 92-iduna-routes.yaml).
 *   IDUNA_PROBE_TIMEOUT_MS  one timeout for every probe; unset = discover.prn's per-candidate defaults.
 *   IDUNA_LOCAL_FIRST       0 skips the local probe; 1 (or unset) probes it.
 * stdout: the chosen base URL, without a trailing slash. stderr: one line per candidate, then the outcome (-q: none).
 * Exit: 0  an IDUNA answered its health check: use it.
 *       2  no IDUNA answered (or the cloud could not be checked: an https base needs TLS, which PARENA's C runtime does
 *          not have yet). The printed base is the cloud, ATTEMPTED rather than confirmed; the tool's own first call
 *          gives the real error.
 *       1  a URL in the environment can't be used (the message names it).
 * "For the session": capture the answer once per shell (the export above) rather than probing on every call. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int https;
    char host[256];
    int port;
    char base_path[512]; /* "" or "/prefix", never a trailing slash */
    char base[800];      /* the URL as printed: scheme://host[:port]/prefix, no trailing slash */
} IdunaURL;

/* scheme://host[:port][/path]. IPv6 literals in brackets. Query or fragment: refused. 1 ok, 0 not usable. */
static int iduna_parse_url(const char *url, IdunaURL *u) {
    const char *p, *host_start, *host_end, *path;
    size_t hl, pl;
    memset(u, 0, sizeof *u);
    if (strncmp(url, "http://", 7) == 0) { u->https = 0; p = url + 7; }
    else if (strncmp(url, "https://", 8) == 0) { u->https = 1; p = url + 8; }
    else return 0;
    if (strpbrk(p, "?#@ ") != NULL) return 0;
    if (*p == '[') {
        host_start = p + 1;
        host_end = strchr(host_start, ']');
        if (host_end == NULL) return 0;
        p = host_end + 1;
    } else {
        host_start = p;
        while (*p && *p != ':' && *p != '/') p++;
        host_end = p;
    }
    hl = (size_t)(host_end - host_start);
    if (hl == 0 || hl >= sizeof u->host) return 0;
    memcpy(u->host, host_start, hl);
    u->port = u->https ? 443 : 80;
    if (*p == ':') {
        char *end;
        long port = strtol(p + 1, &end, 10);
        if (end == p + 1 || port <= 0 || port > 65535) return 0;
        u->port = (int)port;
        p = end;
    }
    if (*p != '\0' && *p != '/') return 0;
    path = p;
    pl = strlen(path);
    while (pl > 0 && path[pl - 1] == '/') pl--;
    if (pl >= sizeof u->base_path) return 0;
    memcpy(u->base_path, path, pl);
    u->base_path[pl] = '\0';
    pl = strlen(url);
    while (pl > 0 && url[pl - 1] == '/') pl--;
    if (pl >= sizeof u->base) return 0;
    memcpy(u->base, url, pl);
    u->base[pl] = '\0';
    return 1;
}

static const char *iduna_env(const char *name, const char *def) {
    const char *v = getenv(name);
    return (v != NULL && v[0] != '\0') ? v : def;
}

static int g_quiet = 0;

static void iduna_say(const char *who, const IdunaURL *u, int verdict, int status, int timeout_ms, long long ms) {
    if (g_quiet) return;
    if (verdict == verdict_iduna())
        fprintf(stderr, "iduna-discover: %s %s -> IDUNA (%lld ms)\n", who, u->base, ms);
    else if (verdict == verdict_not_iduna())
        fprintf(stderr, "iduna-discover: %s %s -> answered HTTP %d, but not as IDUNA\n", who, u->base, status);
    else if (verdict == verdict_timeout())
        fprintf(stderr, "iduna-discover: %s %s -> no answer within %d ms\n", who, u->base, timeout_ms);
    else
        fprintf(stderr, "iduna-discover: %s %s -> not running (nothing listening, or no such host)\n", who, u->base);
}

/* One candidate: its verdict (discover.prn codes). An https base can't be probed here and counts as no response. */
static int iduna_probe(const char *who, const IdunaURL *u, int timeout_ms) {
    Arena scratch;
    char path[600];
    long long t0;
    int verdict;
    if (u->https) {
        if (!g_quiet)
            fprintf(stderr, "iduna-discover: %s %s -> not checked: https needs TLS, which PARENA's C runtime does not "
                            "have yet\n", who, u->base);
        return verdict_unreachable();
    }
    snprintf(path, sizeof path, "%s/health", u->base_path);
    arena_init(&scratch);
    t0 = http_probe_now_ms();
    verdict = probe_iduna_at((char *)u->host, u->port, path, timeout_ms, &scratch);
    iduna_say(who, u, verdict, probe_status(), timeout_ms, http_probe_now_ms() - t0);
    arena_free_all(&scratch);
    return verdict;
}

int main(int argc, char **argv) {
    IdunaURL local_url, cloud_url;
    const char *local_s = iduna_env("IDUNA_LOCAL_URL", "http://localhost:8080");
    const char *cloud_s = iduna_env("IDUNA_CLOUD_URL", "https://iam.okemily.com");
    int configured = atoi(iduna_env("IDUNA_PROBE_TIMEOUT_MS", "0"));
    const char *lf = iduna_env("IDUNA_LOCAL_FIRST", "");
    int opt = strcmp(lf, "0") == 0 ? opt_local_off() : (strcmp(lf, "1") == 0 ? opt_local_on() : opt_default());
    int try_local = should_try_local(page_native(), opt);
    int cand = -1;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) g_quiet = 1;
        else {
            fprintf(stderr, "usage: iduna-discover [-q]   (env IDUNA_LOCAL_URL, IDUNA_CLOUD_URL, IDUNA_PROBE_TIMEOUT_MS, "
                            "IDUNA_LOCAL_FIRST)\n");
            return 1;
        }
    }
    if (!iduna_parse_url(local_s, &local_url)) {
        fprintf(stderr, "iduna-discover: IDUNA_LOCAL_URL=%s is not an http(s)://host[:port][/path] URL\n", local_s);
        return 1;
    }
    if (!iduna_parse_url(cloud_s, &cloud_url)) {
        fprintf(stderr, "iduna-discover: IDUNA_CLOUD_URL=%s is not an http(s)://host[:port][/path] URL\n", cloud_s);
        return 1;
    }
    /* A native tool has no page of its own, so there is no same-origin candidate. */
    while ((cand = next_candidate(cand, 0, try_local)) != cand_done()) {
        const IdunaURL *u = cand == cand_local() ? &local_url : &cloud_url;
        const char *who = cand == cand_local() ? "local" : "cloud";
        if (verdict_usable(iduna_probe(who, u, probe_timeout_ms(cand, configured))) == 1) {
            if (!g_quiet) fprintf(stderr, "iduna-discover: using %s IDUNA %s\n", who, u->base);
            printf("%s\n", u->base);
            return 0;
        }
    }
    if (!g_quiet)
        fprintf(stderr, "iduna-discover: no IDUNA answered; attempting the cloud IDUNA %s\n", cloud_url.base);
    printf("%s\n", fallback_source() == cand_cloud() ? cloud_url.base : local_url.base);
    return 2;
}
