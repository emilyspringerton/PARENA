/* tests/test_log_projector.c -- real end-to-end verification of stdlib/log/projector.prn's own
 * SQL-generation functions (LO FRAMEWORK_NORTHSTAR.md's own event-sourcing extension, founder
 * real-time: "continue building the framework with jsonl log streaming with mysql psql sqlite
 * etc projectors"). Real, honest scope, matching that file's own header comment: this sandbox
 * has no sqlite3/psql CLI installed and no usable MySQL credentials (checked directly, not
 * assumed -- see EMILY/BACKLOG.md's own S225 note and sudo-queue/NOT_INCLUDED.md's pre-existing
 * MySQL-credentials gap), so this test verifies the real SQL TEXT each function generates, plus
 * a real, live shell round trip through project-sqlite!/project-mysql! themselves -- shimmed via
 * a real, temporary PATH directory holding stand-in `sqlite3`/`mysql` scripts, so the exact real
 * command construction (`sqlite3 <db> "<sql>"`) is exercised end-to-end, not just its own text.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "test_log_projector_gen.c"

static void write_stub(const char *path, const char *body) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs(body, f);
    fclose(f);
    chmod(path, 0755);
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real DDL text, shared identically across all three dialects. */
    assert(strcmp(events_table_ddl(),
        "CREATE TABLE IF NOT EXISTS events (kind TEXT NOT NULL, id TEXT NOT NULL, "
        "op TEXT NOT NULL, fields TEXT NOT NULL, ts INTEGER NOT NULL);") == 0);

    /* Real single-quote escaping. */
    assert(strcmp(sql_escape_string("O'Brien", &arena), "O''Brien") == 0);
    assert(strcmp(sql_escape_string("no quotes here", &arena), "no quotes here") == 0);

    /* Real INSERT text for a real Event, values correctly escaped. */
    Event e1 = {"Repo", "repo-1", "create", "{\"name\":\"O'Brien's Repo\"}", 1000};
    char *sql1 = insert_event_sql(&e1, &arena);
    assert(strcmp(sql1,
        "INSERT INTO events (kind, id, op, fields, ts) VALUES "
        "('Repo', 'repo-1', 'create', '{\"name\":\"O''Brien''s Repo\"}', 1000);") == 0);

    /* Real, live shell-invocation plumbing: a temporary PATH directory holding stand-in
     * `sqlite3`/`mysql` scripts, standing in for the real CLI clients this sandbox doesn't have
     * installed/credentialed -- proves project-sqlite!/project-mysql! really do shell out with
     * the right command shape, not just that their own SQL-text generation looks right. */
    const char *bindir = "/tmp/test_log_projector_stubs";
    mkdir(bindir, 0755);
    const char *out_path = "/tmp/test_log_projector_stub_out.txt";
    unlink(out_path);

    char stub_path[256];
    snprintf(stub_path, sizeof(stub_path), "%s/sqlite3", bindir);
    char stub_body[512];
    snprintf(stub_body, sizeof(stub_body), "#!/bin/sh\necho \"$@\" > %s\nexit 0\n", out_path);
    write_stub(stub_path, stub_body);

    char old_path[8192];
    const char *real_path = getenv("PATH");
    snprintf(old_path, sizeof(old_path), "%s", real_path ? real_path : "");
    char new_path[8192 + 64];
    snprintf(new_path, sizeof(new_path), "%s:%s", bindir, old_path);
    setenv("PATH", new_path, 1);

    Result r = project_sqlite_("/tmp/test_repos.db", &e1, &arena);
    assert(r.tag == 1);

    FILE *out = fopen(out_path, "r");
    assert(out != NULL);
    char buf[4096];
    assert(fgets(buf, sizeof(buf), out) != NULL);
    fclose(out);
    assert(strstr(buf, "/tmp/test_repos.db") != NULL);
    assert(strstr(buf, "CREATE TABLE IF NOT EXISTS events") != NULL);
    assert(strstr(buf, "INSERT INTO events") != NULL);
    assert(strstr(buf, "repo-1") != NULL);

    /* A real, nonzero exit code (a failing "DB client") is reported as a real Err, not
     * silently treated as success. */
    snprintf(stub_path, sizeof(stub_path), "%s/mysql", bindir);
    write_stub(stub_path, "#!/bin/sh\nexit 1\n");
    Result r2 = project_mysql_("shithub", &e1, &arena);
    assert(r2.tag == 0);

    setenv("PATH", old_path, 1);
    unlink(out_path);
    printf("test_log_projector: all assertions passed\n");
    return 0;
}
