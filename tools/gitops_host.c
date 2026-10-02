/* tools/gitops_host.c -- `parena-gitops`: thin host for stdlib/k8s/gitops.prn.
 *
 * Pull-based GitOps reconciler. Each tick: `git fetch` + fast-forward the manifests repo, compare
 * HEAD to the last successfully applied revision, ask the PARENA decision (gitops_decide), and
 * run `kubectl apply` when told to. All decision logic is in PARENA; this file is only argv,
 * process spawning and a two-file state dir (same host/logic split as parenabusybox_host.c).
 *
 * STOPGAP (CLAUDE.md "Core Deps Are PARENA-First"): shells out to the real `git` and `kubectl`
 * binaries. Those are system tools, not product logic, but a PARENA-native apply client is a
 * tracked replacement item (EMILY/BACKLOG.md), not forgotten.
 *
 * usage: parena-gitops -r <repo> -p <manifest-subdir> -s <state-dir> [-i secs] [--once] [--dry-run]
 *   --dry-run  print the decision, never touch the cluster (kubectl is not invoked)
 */
#include "parena_runtime.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gitops_gen.c"

static int sh(const char *cmd, char *out, size_t n) {
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t len = 0;
    if (out) { out[0] = 0; while (len + 1 < n && fgets(out + len, (int)(n - len), p)) len = strlen(out); }
    else { char buf[256]; while (fgets(buf, sizeof buf, p)) fputs(buf, stdout); }
    int rc = pclose(p);
    if (out) { while (len && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = 0; }
    return rc;
}

static void read_file(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}

static void write_file(const char *path, const char *s) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(s, f); fclose(f); }
}

int main(int argc, char **argv) {
    const char *repo = NULL, *sub = ".", *state = NULL;
    int interval = 30; bool once = false, dry = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) repo = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) sub = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) state = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--once")) once = true;
        else if (!strcmp(argv[i], "--dry-run")) dry = true;
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    if (!repo || !state) { fprintf(stderr, "usage: parena-gitops -r <repo> -p <subdir> -s <state-dir> [-i secs] [--once] [--dry-run]\n"); return 2; }

    char cmd[2048], head[128], applied[128], att[32], failrev[128], p_applied[1024], p_att[1024], p_fail[1024];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", state); sh(cmd, NULL, 0);
    snprintf(p_applied, sizeof p_applied, "%s/applied_rev", state);
    snprintf(p_att, sizeof p_att, "%s/attempt", state);
    snprintf(p_fail, sizeof p_fail, "%s/failed_rev", state);

    for (;;) {
        snprintf(cmd, sizeof cmd, "git -C '%s' pull --ff-only --quiet >/dev/null 2>&1 || true", repo); sh(cmd, NULL, 0);
        snprintf(cmd, sizeof cmd, "git -C '%s' rev-parse HEAD", repo);
        if (sh(cmd, head, sizeof head) != 0 || !head[0]) { fprintf(stderr, "gitops: cannot read HEAD of %s\n", repo); return 1; }
        read_file(p_applied, applied, sizeof applied);
        read_file(p_att, att, sizeof att);
        read_file(p_fail, failrev, sizeof failrev);
        /* failures belong to the revision that failed; a new HEAD starts clean */
        int attempt = strcmp(failrev, head) == 0 ? atoi(att) : 0;
        /* new = differs from what's applied AND hasn't already failed (a failed rev is retried/held, not "new") */
        bool changed = strcmp(head, applied) != 0 && strcmp(head, failrev) != 0;
        bool last_ok = attempt == 0;

        Arena a; arena_init(&a);
        int action = gitops_decide(changed, last_ok, attempt);
        printf("gitops: head=%.12s applied=%.12s attempt=%d action=%d\n", head, applied[0] ? applied : "-", attempt, action);

        if (action == 1 || action == 2) {
            if (dry) { printf("gitops: dry-run, would: kubectl apply -R -f %s/%s\n", repo, sub); }
            else {
                snprintf(cmd, sizeof cmd, "kubectl apply -R -f '%s/%s'", repo, sub);
                if (sh(cmd, NULL, 0) == 0) { write_file(p_applied, head); write_file(p_att, "0"); write_file(p_fail, ""); }
                else { char b[16]; snprintf(b, sizeof b, "%d", attempt + 1); write_file(p_att, b); write_file(p_fail, head); fprintf(stderr, "gitops: apply failed (attempt %d)\n", attempt + 1); }
            }
        } else if (action == 3) {
            fprintf(stderr, "gitops: HOLD — %d consecutive apply failures at %.12s; needs a human\n", attempt, head);
        }
        if (once) return action == 3 ? 3 : 0;
        sleep((unsigned)gitops_requeue_seconds(action, interval, attempt, 300));
    }
}
