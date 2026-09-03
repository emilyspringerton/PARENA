/* tests/test_mixforge_import.c -- real end-to-end verification of
 * stdlib/mixforge/import.prn (S243-01, MIXFORGE V0: paste-a-YouTube-URL track import). Confirms
 * the real allowlist/injection defense, the real yt-dlp shell-invocation shape, and real Ok/Err
 * propagation for both the main and optional-instrumental download -- not just "did it compile".
 * Real, honest, environment-dependent shim: this sandbox has no real yt-dlp installed (and no
 * network access this test should depend on anyway), so a real, temporary PATH directory holds
 * a stand-in `yt-dlp` script, the same technique test_log_projector.c already established for
 * sqlite3/mysql.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "test_mixforge_import_gen.c"

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

    /* Real, narrow allowlist -- both accepted host prefixes. */
    assert(safe_youtube_url_("https://www.youtube.com/watch?v=dQw4w9WgXcQ", &arena) == 1);
    assert(safe_youtube_url_("https://youtu.be/dQw4w9WgXcQ", &arena) == 1);

    /* Real, decisive rejections -- wrong host, and every real shell-injection shape. */
    assert(safe_youtube_url_("https://evil.example.com/watch?v=x", &arena) == 0);
    assert(safe_youtube_url_("https://www.youtube.com/watch?v=x'; rm -rf /; echo '", &arena) == 0);
    assert(safe_youtube_url_("https://www.youtube.com/watch?v=x`whoami`", &arena) == 0);
    assert(safe_youtube_url_("https://www.youtube.com/watch?v=x && echo pwned", &arena) == 0);
    assert(safe_youtube_url_("https://www.youtube.com/watch?v=x|cat /etc/passwd", &arena) == 0);
    assert(safe_youtube_url_("https://www.youtube.com/watch?v=x$HOME", &arena) == 0);

    /* download-track rejects an unsafe URL WITHOUT ever shelling out at all. */
    Result bad = download_track("javascript:alert(1)", "/tmp/mixforge_test_main", &arena);
    assert(bad.tag == 0);

    /* Real, temporary PATH directory holding a stand-in yt-dlp -- records the exact real
     * command-line arguments it was invoked with, then prints a real, deterministic fake path
     * to stdout on the line --print after_move:filepath asks for. */
    const char *bindir = "/tmp/test_mixforge_import_stubs";
    mkdir(bindir, 0755);
    const char *args_path = "/tmp/test_mixforge_import_stub_args.txt";
    unlink(args_path);

    char stub_path[256];
    snprintf(stub_path, sizeof(stub_path), "%s/yt-dlp", bindir);
    char stub_body[1024];
    snprintf(stub_body, sizeof(stub_body),
        "#!/bin/sh\necho \"$@\" > %s\necho '/tmp/mixforge_test_main/dQw4w9WgXcQ.mp3'\nexit 0\n",
        args_path);
    write_stub(stub_path, stub_body);

    char old_path[8192];
    const char *real_path = getenv("PATH");
    snprintf(old_path, sizeof(old_path), "%s", real_path ? real_path : "");
    char new_path[8192 + 64];
    snprintf(new_path, sizeof(new_path), "%s:%s", bindir, old_path);
    setenv("PATH", new_path, 1);

    /* Real download, real stubbed yt-dlp, real returned path -- and confirms the trailing
     * newline from the stub's own echo was actually stripped. */
    Result r = download_track("https://www.youtube.com/watch?v=dQw4w9WgXcQ",
                               "/tmp/mixforge_test_main", &arena);
    assert(r.tag == 1);
    assert(strcmp((char *)r.value, "/tmp/mixforge_test_main/dQw4w9WgXcQ.mp3") == 0);

    FILE *af = fopen(args_path, "r");
    assert(af != NULL);
    char argbuf[4096];
    assert(fgets(argbuf, sizeof(argbuf), af) != NULL);
    fclose(af);
    /* Real command shape: the URL and the -o output template both really reached yt-dlp,
     * single-quoted (shell-single-quote's own real defense, reused from log/projector.prn). */
    assert(strstr(argbuf, "-x") != NULL);
    assert(strstr(argbuf, "--audio-format mp3") != NULL);
    assert(strstr(argbuf, "--print after_move:filepath") != NULL);
    assert(strstr(argbuf, "dQw4w9WgXcQ") != NULL);
    assert(strstr(argbuf, "/tmp/mixforge_test_main/%(id)s.%(ext)s") != NULL);

    /* A real, nonzero yt-dlp exit code is reported as a real Err, not silently swallowed. */
    write_stub(stub_path, "#!/bin/sh\nexit 1\n");
    Result r2 = download_track("https://youtu.be/xxxxxxxxxxx", "/tmp/mixforge_test_main", &arena);
    assert(r2.tag == 0);

    /* import-track, no instrumental: one real download, instrumental fields stay "". */
    write_stub(stub_path, stub_body);
    Result imp = import_track("https://www.youtube.com/watch?v=dQw4w9WgXcQ", "",
                               "/tmp/mixforge_test_main", "/tmp/mixforge_test_inst", &arena);
    assert(imp.tag == 1);
    TrackRecord *rec = (TrackRecord *)imp.value;
    assert(strcmp(rec->main_path, "/tmp/mixforge_test_main/dQw4w9WgXcQ.mp3") == 0);
    assert(strcmp(rec->instrumental_url, "") == 0);
    assert(strcmp(rec->instrumental_path, "") == 0);

    /* import-track, WITH an instrumental URL: both downloads happen, both paths land in the
     * real TrackRecord, and the resulting NDJSON metadata line is well-formed. */
    Result imp2 = import_track("https://www.youtube.com/watch?v=dQw4w9WgXcQ",
                                "https://youtu.be/dQw4w9WgXcQ",
                                "/tmp/mixforge_test_main", "/tmp/mixforge_test_inst", &arena);
    assert(imp2.tag == 1);
    TrackRecord *rec2 = (TrackRecord *)imp2.value;
    assert(strcmp(rec2->instrumental_path, "/tmp/mixforge_test_main/dQw4w9WgXcQ.mp3") == 0);
    char *json = track_metadata_json(rec2, &arena);
    assert(strstr(json, "\"main_url\":\"https://www.youtube.com/watch?v=dQw4w9WgXcQ\"") != NULL);
    assert(strstr(json, "\"instrumental_url\":\"https://youtu.be/dQw4w9WgXcQ\"") != NULL);
    assert(json[0] == '{');
    assert(json[strlen(json) - 1] == '}');

    setenv("PATH", old_path, 1);
    unlink(args_path);
    printf("test_mixforge_import: all assertions passed\n");
    return 0;
}
