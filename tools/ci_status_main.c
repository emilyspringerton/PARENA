/* tools/ci_status_main.c -- the real, minimal C entry point linking
 * stdlib/ci/status.prn's own generated `check()` function together
 * with tools/ci_status_host.c's own real host implementation, plus a
 * standalone `main()` reading config from env vars (GH_REPO/GH_SHA/
 * GITHUB_TOKEN, matching every other real ops script in this monorepo's
 * own convention) and printing a real, human-readable result. This is
 * the temporary, standalone-binary shape this tool runs as until it's
 * wired into `parena`'s own CLI subcommand dispatch (src/main.c) --
 * the real, next step, not yet done.
 */
#include <stdio.h>
#include <stdlib.h>

extern int check(char *repo, char *sha, char *token);

int main(void) {
    char *repo = getenv("GH_REPO");
    char *sha = getenv("GH_SHA");
    char *token = getenv("GITHUB_TOKEN");
    if (!repo || !sha || !token) {
        fprintf(stderr, "ci-status: GH_REPO, GH_SHA, and GITHUB_TOKEN must all be set\n");
        return 3;
    }

    int code = check(repo, sha, token);
    switch (code) {
        case 0:
            printf("✓ %s @ %s: all checks completed, all conclusions success\n", repo, sha);
            break;
        case 1:
            printf("… %s @ %s: still pending\n", repo, sha);
            break;
        case 2:
            printf("✗ %s @ %s: completed, but at least one check failed\n", repo, sha);
            break;
        default:
            printf("? %s @ %s: no check-runs found, or the request itself failed\n", repo, sha);
            break;
    }
    return code;
}
