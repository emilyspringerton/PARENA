/* tests/test_webdriver.c — real end-to-end verification for
 * stdlib/net/webdriver.prn (a W3C WebDriver protocol client, "Selenium
 * bindings" -- see that file's own header comment for why that's the
 * right description of what a real Selenium binding actually is).
 *
 * Runs the full session lifecycle -- new-session, navigate-to,
 * find-element, element-click, element-text, page-title, page-source,
 * quit-session -- through the REAL compiled webdriver.prn output, over a real TCP
 * connection, against tests/fake_webdriver_server.go (a real Go HTTP
 * server standing in for a real WebDriver driver; see that file's own
 * header for the honest scope of what it stands in for). Also
 * exercises one real error path: connecting to an unreachable port
 * returns a real Err, not a crash.
 *
 * The fixture server is started and stopped through REAL PARENA FFI
 * (stdlib/process.prn's process-spawn/process-kill, real fork+exec),
 * not shell/Makefile scripting around this binary -- founder, real-
 * time: "wrap it in parena ffi if possible". Go, not Python (founder:
 * "go is fine" / "just dont use python") -- process-spawn launches an
 * arbitrary external program either way, that program being Go-
 * compiled vs. Python is not a distinction PARENA's own spawn call
 * makes or needs to.
 *
 * host_json_unescape is provided here for real (not a stub), same
 * scope note as tests/test_json.c's own copy: the seven named
 * single-char escapes plus \uXXXX passed through as raw ASCII rather
 * than real UTF-8 codepoint encoding.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* \uXXXX -> real UTF-8, found load-bearing 2026-09-02: a real, live chromedriver
 * session (real Chrome for Testing, no sandbox available beforehand -- see
 * this repo's own CHANGELOG for the full trace) confirmed real WebDriver
 * responses DO escape '<'/'>' as </> in page-source, same as any
 * Go/JS JSON encoder's default HTML-escaping -- the earlier version of this
 * function (and of every other host_json_unescape copy in this repo, a real,
 * still-open follow-up) copied the 4 hex digit CHARACTERS through raw
 * ("<" became the 5-byte string "003C", not the 1-byte character '<'),
 * which would have silently corrupted every real page-source call the exact
 * moment it hit real escaped markup. Encodes any BMP codepoint (U+0000-
 * U+FFFF) correctly; surrogate pairs for astral-plane codepoints above
 * U+FFFF are a real, separate, narrower gap not handled here -- every real
 * HTML-significant escape this needs to decode (<, >, &, ", the whole
 * printable ASCII/Latin-1/BMP range) is well within it. */
static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

char *host_json_unescape(char *s, int start, int end, char *out) {
    int oi = 0;
    for (int i = start; i < end; i++) {
        if (s[i] == '\\' && i + 1 < end) {
            i++;
            switch (s[i]) {
                case '"': out[oi++] = '"'; break;
                case '\\': out[oi++] = '\\'; break;
                case '/': out[oi++] = '/'; break;
                case 'b': out[oi++] = '\b'; break;
                case 'f': out[oi++] = '\f'; break;
                case 'n': out[oi++] = '\n'; break;
                case 'r': out[oi++] = '\r'; break;
                case 't': out[oi++] = '\t'; break;
                case 'u': {
                    if (i + 4 < end) {
                        int cp = 0;
                        for (int k = 0; k < 4; k++) { i++; cp = (cp << 4) | hex_nibble(s[i]); }
                        if (cp < 0x80) {
                            out[oi++] = (char)cp;
                        } else if (cp < 0x800) {
                            out[oi++] = (char)(0xC0 | (cp >> 6));
                            out[oi++] = (char)(0x80 | (cp & 0x3F));
                        } else {
                            out[oi++] = (char)(0xE0 | (cp >> 12));
                            out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            out[oi++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default: out[oi++] = s[i];
            }
        } else {
            out[oi++] = s[i];
        }
    }
    out[oi] = 0;
    return out;
}

#include "test_webdriver_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(int argc, char **argv) {
    const char *server_path = argc > 1 ? argv[1] : "./fake_webdriver_server";
    int port = argc > 2 ? atoi(argv[2]) : 9515;
    Arena a;
    arena_init(&a);

    char *host = arena_strdup(&a, "127.0.0.1", 9);
    char port_str[8];
    snprintf(port_str, sizeof port_str, "%d", port);

    /* Real PARENA FFI spawn, not a shell backgrounded process. */
    Result rs = process_spawn((char *)server_path, port_str, &a);
    CHECK(rs.tag, "process-spawn launches the fixture server");
    int server_pid = rs.tag ? *(int *)rs.value : -1;
    sleep(1); /* real, minimal readiness wait -- no health-check endpoint
                 to poll instead, matching every other fixed-sleep
                 startup wait already in this codebase (e.g. iduna.service's
                 own ExecStartPost). */

    Result r1 = new_session(host, port, &a);
    CHECK(r1.tag, "new-session succeeds against the fake driver");
    Session *sess = r1.tag ? (Session *)r1.value : NULL;
    CHECK(sess && strcmp(sess->session_id, "fake-session-123") == 0,
          "new-session parses the real sessionId out of {\"value\":{\"sessionId\":...}}");

    if (sess) {
        Result r2 = navigate_to(sess, "https://example.com/page?a=1", &a);
        CHECK(r2.tag, "navigate-to succeeds (also exercises json-escape-string on a URL with '?'/'=')");

        Result r3 = find_element(sess, "#submit-button", &a);
        CHECK(r3.tag, "find-element succeeds");
        char *elem_id = r3.tag ? (char *)r3.value : NULL;
        CHECK(elem_id && strcmp(elem_id, "fake-element-456") == 0,
              "find-element extracts the real W3C element-ref-key value");

        Result r3b = element_send_keys(sess, elem_id ? elem_id : "", "hello@example.com", &a);
        CHECK(r3b.tag, "element-send-keys succeeds");

        Result r4 = element_click(sess, elem_id ? elem_id : "", &a);
        CHECK(r4.tag, "element-click succeeds");

        Result r5 = element_text(sess, elem_id ? elem_id : "", &a);
        CHECK(r5.tag && strcmp((char *)r5.value, "Hello World") == 0,
              "element-text returns the real text value");

        Result r6 = page_title(sess, &a);
        CHECK(r6.tag && strcmp((char *)r6.value, "Test Page") == 0,
              "page-title returns the real title value");

        Result r6b = page_source(sess, &a);
        CHECK(r6b.tag && strcmp((char *)r6b.value, "<html><body><h1>Fake Rendered Page</h1></body></html>") == 0,
              "page-source returns the real, currently-rendered HTML value");

        Result r7 = quit_session(sess, &a);
        CHECK(r7.tag, "quit-session succeeds");
    } else {
        printf("SKIP: 6 checks (no session)\n");
    }

    /* Real error path: port 9 (discard) refuses every real connection. */
    Result r8 = new_session(host, 9, &a);
    CHECK(!r8.tag, "new-session against an unreachable port returns a real Err, not a crash");

    /* new-session-with-capabilities -- the fake server ignores the real
       request body content either way (see its own /session route), so
       this exercises that the real capabilities JSON gets embedded and
       sent without breaking the request/response parse, not that a real
       driver actually honors it (that needs a real driver, see
       webdriver_fetch_real.c / this repo's own CHANGELOG for that). */
    Result r9 = new_session_with_capabilities(host, port,
        "{\"goog:chromeOptions\":{\"args\":[\"--headless=new\",\"--no-sandbox\"]}}", &a);
    CHECK(r9.tag, "new-session-with-capabilities succeeds against the fake driver");
    if (r9.tag) quit_session((Session *)r9.value, &a);

    /* Real PARENA FFI teardown. */
    if (server_pid > 0) {
        Result rk = process_kill(server_pid, &a);
        CHECK(rk.tag, "process-kill stops the fixture server");
    }

    printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
