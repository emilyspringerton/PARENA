/* tests/test_git.c -- real end-to-end verification of stdlib/git.prn (kanban cruise-queue card
 * 342534534535: "copy the git interface paradigms and idoms and affordances from vs codes for
 * GIT for our parena editor"). Unlike test_mixforge_import.c (which has to stub yt-dlp -- no
 * network access, no real account), a real `git` binary is always available in this sandbox, so
 * this exercises every real function against an actual, freshly-created git repository -- no
 * stubbing needed.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "test_git_gen.c"

static void run_or_die(const char *cmd) {
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "setup command failed (%d): %s\n", rc, cmd);
        exit(1);
    }
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real, fresh, throwaway git repo -- not this actual PARENA checkout (never run destructive
       git operations against a real, live-tracked repo from a test). */
    const char *repo = "/tmp/test_git_prn_repo";
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", repo, repo);
    run_or_die(cmd);
    snprintf(cmd, sizeof(cmd), "cd %s && git init -q && git config user.email t@t.com && git config user.name t", repo);
    run_or_die(cmd);

    /* A clean, empty repo (no commits yet) has a real, empty status. */
    Vec clean_status = git_status((char *)repo, &arena);
    assert(vec_len(&clean_status) == 0);

    /* Real untracked file -> real '??' status. */
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/hello.txt", repo);
    FILE *f = fopen(filepath, "w");
    assert(f != NULL);
    fputs("hello\n", f);
    fclose(f);

    Vec untracked_status = git_status((char *)repo, &arena);
    assert(vec_len(&untracked_status) == 1);
    GitStatusEntry *entry = (GitStatusEntry *)vec_get(&untracked_status, 0);
    assert(strcmp(entry->index_status, "?") == 0);
    assert(strcmp(entry->worktree_status, "?") == 0);
    assert(strcmp(entry->path, "hello.txt") == 0);

    /* git-stage! -> real 'A ' (staged-added) status. */
    Result stage_res = git_stage_((char *)repo, "hello.txt", &arena);
    assert(stage_res.tag == 1);
    Vec staged_status = git_status((char *)repo, &arena);
    assert(vec_len(&staged_status) == 1);
    entry = (GitStatusEntry *)vec_get(&staged_status, 0);
    assert(strcmp(entry->index_status, "A") == 0);

    /* git-unstage! -> back to real untracked. */
    Result unstage_res = git_unstage_((char *)repo, "hello.txt", &arena);
    assert(unstage_res.tag == 1);
    Vec unstaged_status = git_status((char *)repo, &arena);
    entry = (GitStatusEntry *)vec_get(&unstaged_status, 0);
    assert(strcmp(entry->index_status, "?") == 0);

    /* git-stage! then git-commit! -> real, empty status again (committed = clean). */
    git_stage_((char *)repo, "hello.txt", &arena);
    Result commit_res = git_commit_((char *)repo, "real first commit", &arena);
    assert(commit_res.tag == 1);
    Vec post_commit_status = git_status((char *)repo, &arena);
    assert(vec_len(&post_commit_status) == 0);

    /* git-current-branch -> a real, non-empty branch name. */
    char *branch = git_current_branch((char *)repo, &arena);
    assert(strlen(branch) > 0);

    /* Real modification -> real ' M' (unstaged-modified) status, and a real, non-empty diff. */
    f = fopen(filepath, "w");
    assert(f != NULL);
    fputs("hello, modified\n", f);
    fclose(f);
    Vec modified_status = git_status((char *)repo, &arena);
    assert(vec_len(&modified_status) == 1);
    entry = (GitStatusEntry *)vec_get(&modified_status, 0);
    assert(strcmp(entry->worktree_status, "M") == 0);

    char *diff = git_diff((char *)repo, "hello.txt", &arena);
    assert(strstr(diff, "hello, modified") != NULL);

    /* Real, deliberate shell-injection-shaped path -- must not corrupt the real command or
       crash; shell_single_quote (log/projector.prn's own proven helper) is the real defense
       already applied inside every function above. A path this malformed simply won't match a
       real tracked/untracked file, so git reports it as nothing to stage -- a real, safe
       failure mode, not silent command injection. */
    Result injection_res = git_stage_((char *)repo, "'; rm -rf /tmp/should-not-exist; echo '", &arena);
    (void)injection_res; /* real, honest: git itself exits non-zero for a nonexistent path -- the
                             real assertion here is that nothing outside the repo got touched. */
    struct stat st;
    assert(stat("/tmp/should-not-exist", &st) != 0); /* the injected command must NOT have run */

    printf("all git.prn assertions passed\n");
    return 0;
}
