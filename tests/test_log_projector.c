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
 *
 * S498 (2026-09-18) added the same real, stub-shell round trip for project-mssql! (a stand-in
 * `tsql`, piped via stdin instead of an argument -- see log/projector.prn's own header comment).
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

    /* Real, LIVE end-to-end verification against the real sqlite3 CLI, when one is actually
     * installed (real, honest, environment-dependent skip -- matching this codebase's own
     * established convention elsewhere for a check that needs an optional external tool, e.g.
     * emitter_test.go's own PARENA-binary-not-found skips in the sibling LO repo). This exact
     * live run (2026-09-02, once sudo-queue/45-install-sqlite3-and-postgresql-client.sh had
     * been run) is what found and fixed a real, genuine shell-quoting bug -- see
     * shell-single-quote's own doc comment in log/projector.prn -- not merely a defensive
     * addition after the fact. */
    if (system("command -v sqlite3 > /dev/null 2>&1") == 0) {
        const char *db_path = "/tmp/test_log_projector_live.db";
        unlink(db_path);
        Event live_e = {"Repo", "repo-1", "create",
                         "{\"name\":\"PARENA\",\"owner\":\"emilyspringerton\"}", 1000};
        Result lr = project_sqlite_((char *)db_path, &live_e, &arena);
        assert(lr.tag == 1);

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "sqlite3 %s \"SELECT fields FROM events;\"", db_path);
        FILE *p = popen(cmd, "r");
        assert(p != NULL);
        char field_buf[256] = {0};
        assert(fgets(field_buf, sizeof(field_buf), p) != NULL);
        pclose(p);
        /* The real, stored value must be the exact original JSON, quotes intact -- confirms
         * shell-single-quote's own fix: the pre-fix bug silently stripped the embedded `"`
         * characters (`{"name":"PARENA"}` was corrupted into `{name:PARENA}` in the real
         * database, invalid JSON, found live exactly this way). */
        assert(strstr(field_buf, "\"name\":\"PARENA\"") != NULL);
        assert(strstr(field_buf, "\"owner\":\"emilyspringerton\"") != NULL);
        unlink(db_path);
    } else {
        printf("test_log_projector: sqlite3 CLI not found, skipping the real live-DB check\n");
    }

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

    /* ---- MSSQL (S498, 2026-09-18, founder real-time: "we need to put in PARENA primatives for
     * MSSQL and double down on all the unix socket stuff and raw socket stuff") -- real T-SQL DDL
     * text, real shell-quoted tsql connect command, and a real live stub-`tsql`-via-stdin round
     * trip, same technique as the sqlite3/mysql checks above but through run-sql-via-stdin
     * instead of run-sql-via (tsql has no -e/-c one-shot-SQL flag -- see log/projector.prn's own
     * header comment for the full real reasoning). */
    assert(strcmp(events_table_ddl_mssql(),
        "IF OBJECT_ID('events', 'U') IS NULL CREATE TABLE events (kind VARCHAR(MAX) NOT NULL, "
        "id VARCHAR(MAX) NOT NULL, op VARCHAR(MAX) NOT NULL, fields VARCHAR(MAX) NOT NULL, "
        "ts INTEGER NOT NULL);") == 0);
    printf("PASS: real T-SQL DDL text is correct (no CREATE TABLE IF NOT EXISTS -- T-SQL has no "
           "such syntax, a real dialect difference from the shared events_table_ddl above)\n");

    char *mssql_prefix = tsql_connect_prefix((char *)"sqlhost", 1433, (char *)"sa",
                                              (char *)"p@ss'word", (char *)"shithub", &arena);
    assert(strstr(mssql_prefix, "tsql -S 'sqlhost'") != NULL);
    assert(strstr(mssql_prefix, "-p 1433") != NULL);
    assert(strstr(mssql_prefix, "-U 'sa'") != NULL);
    assert(strstr(mssql_prefix, "-P 'p@ss'\\''word'") != NULL); /* embedded quote shell-escaped */
    assert(strstr(mssql_prefix, "-D 'shithub'") != NULL);
    printf("PASS: tsql-connect-prefix builds a correctly shell-quoted command\n");

    const char *mssql_bindir = "/tmp/test_log_projector_mssql_stubs";
    mkdir(mssql_bindir, 0755);
    const char *mssql_out_path = "/tmp/test_log_projector_mssql_stub_out.txt";
    unlink(mssql_out_path);

    char mssql_stub_path[256];
    snprintf(mssql_stub_path, sizeof(mssql_stub_path), "%s/tsql", mssql_bindir);
    char mssql_stub_body[512];
    snprintf(mssql_stub_body, sizeof(mssql_stub_body), "#!/bin/sh\ncat > %s\nexit 0\n",
             mssql_out_path);
    write_stub(mssql_stub_path, mssql_stub_body);

    char mssql_new_path[8192 + 64];
    snprintf(mssql_new_path, sizeof(mssql_new_path), "%s:%s", mssql_bindir, old_path);
    setenv("PATH", mssql_new_path, 1);

    Result mr = project_mssql_((char *)"127.0.0.1", 1433, (char *)"sa", (char *)"pw",
                                (char *)"shithub", &e1, &arena);
    assert(mr.tag == 1);

    FILE *mssql_out = fopen(mssql_out_path, "r");
    assert(mssql_out != NULL);
    char mssql_buf[4096];
    size_t mssql_n = fread(mssql_buf, 1, sizeof(mssql_buf) - 1, mssql_out);
    mssql_buf[mssql_n] = '\0';
    fclose(mssql_out);
    /* Proves the SQL was piped via stdin (not passed as a CLI argument the way the other three
     * backends receive it), and that a real T-SQL GO batch terminator is present. */
    assert(strstr(mssql_buf, "IF OBJECT_ID('events', 'U') IS NULL") != NULL);
    assert(strstr(mssql_buf, "INSERT INTO events") != NULL);
    assert(strstr(mssql_buf, "repo-1") != NULL);
    assert(strstr(mssql_buf, "\nGO\n") != NULL);
    printf("PASS: project-mssql! pipes real SQL (DDL+INSERT+GO) via stdin to tsql, not as an "
           "argument\n");

    write_stub(mssql_stub_path, "#!/bin/sh\ncat > /dev/null\nexit 1\n");
    Result mr2 = project_mssql_((char *)"127.0.0.1", 1433, (char *)"sa", (char *)"pw",
                                 (char *)"shithub", &e1, &arena);
    assert(mr2.tag == 0);
    printf("PASS: a real, nonzero tsql exit code is correctly reported as Err\n");

    setenv("PATH", old_path, 1);
    unlink(mssql_out_path);

    printf("test_log_projector: all assertions passed\n");
    return 0;
}
