# Bazel (MODULE.bazel, src/BUILD.bazel, tests/BUILD.bazel) is CI's and this
# repo's primary build system as of 2026-08-20 -- `bazel build //...` /
# `bazel test //tests:test_lexer_parser`. This Makefile is kept alongside
# for the fastest possible local edit-compile-test loop (no Bazel analysis
# overhead for a 5-file C project) — both stay in sync by hand since
# there's no codegen step between them yet.
CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -pedantic -g

SRC := src/arena.c src/ast.c src/lexer.c src/parser.c src/region.c src/emit.c src/fmt.c
OBJ := $(SRC:.c=.o)

.PHONY: all build test test-domain4 test-domain5 test-multifile test-webdriver test-shell test-sdl2 turbogrep clean

all: build

# Real, deliberate TWO-STAGE bootstrap (2026-08-21, founder: "add it to
# the parena cli"): `parena ci-status` calls into `check()`, the C
# function stdlib/ci/status.prn itself compiles down to (a REAL PARENA
# module, not hand-written C -- see that file's own header comment) --
# but that C doesn't exist until SOME already-built parena has compiled
# it. Stage 1 (.parena-bootstrap) is an ordinary build with the
# ci-status subcommand compiled OUT (PARENA_HAS_CI_STATUS undefined --
# see main.c's own comment on that macro); it exists only to generate
# tools/ci_status_gen.c. Stage 2 (the real, shipped `parena` binary)
# recompiles main.c WITH that macro defined, this time linking the
# freshly-generated module C plus its own real host implementation
# (tools/ci_status_host.c) in. `build`'s own target is still just
# `parena` -- callers never need to know this is two stages under the
# hood.
build: parena

.parena-bootstrap: src/main.c $(OBJ)
	$(CC) $(CFLAGS) -o .parena-bootstrap src/main.c $(OBJ)

tools/ci_status_gen.c: .parena-bootstrap stdlib/ci/status.prn
	./.parena-bootstrap build stdlib/ci/status.prn -o tools/ci_status_gen.c

parena: src/main.c $(OBJ) tools/ci_status_gen.c tools/ci_status_host.c tools/ci_status.h
	$(CC) $(CFLAGS) -DPARENA_HAS_CI_STATUS -I runtime -I tools -include tools/ci_status.h \
		-o parena src/main.c $(OBJ) tools/ci_status_gen.c tools/ci_status_host.c

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: tests/test_lexer_parser tests/test_region tests/test_emit
	./tests/test_lexer_parser
	./tests/test_region
	./tests/test_emit

# VS0 domain 4 -- runs the real emitted program under ASan+UBSan (and
# Valgrind, if installed), plus a deliberately-broken fixture proving
# the check has teeth. Not part of the default `test` target since it
# shells out to gcc/ASan directly rather than using this Makefile's own
# $(CFLAGS)/$(OBJ) plumbing -- a real, separate integration check.
test-domain4: build
	bash tests/integration/run_domain4_check.sh

# VS0 domain 5 -- the DoD's own literal CLI Runner acceptance bar: a real
# valid file exits 0 and writes real output, a real region-safety
# violation exits 1 and leaves no stale output file behind. Not folded
# into the plain `test` target for the same "separate, real integration
# check, not this Makefile's own $(CFLAGS)/$(OBJ) plumbing" reason
# test-domain4 already isn't.
test-domain5: build
	bash tests/integration/run_domain5_check.sh

test-multifile: build
	bash tests/integration/run_multifile_check.sh

# test-json -- real end-to-end verification for stdlib/json.prn (parsing
# actual JSON text and checking the resulting structure, not just "does
# it compile"). tests/test_json_gen.c is committed (matches how other
# generated stdlib .c isn't recreated fresh on every run elsewhere in
# this repo) but regenerated here to catch drift if json.prn changes.
test-json: build
	./parena build stdlib/string.prn stdlib/json.prn -o tests/test_json_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_json.c runtime/parena_runtime.c \
		-o /tmp/test_json_bin -lm
	/tmp/test_json_bin

# test-yaml -- real end-to-end verification for stdlib/yaml.prn, same
# discipline as test-json above (real parsing + structure checks, not
# just "does it compile").
test-yaml: build
	./parena build stdlib/string.prn stdlib/yaml.prn -o tests/test_yaml_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_yaml.c runtime/parena_runtime.c \
		-o /tmp/test_yaml_bin -lm
	/tmp/test_yaml_bin

# test-awk -- real end-to-end verification for stdlib/expr.prn (the
# arithmetic/string/variable-binding expression evaluator) and
# stdlib/awk.prn's own read-split-match-evaluate pipeline, same
# discipline as test-json/test-yaml above. Deliberately NOT a
# `turboawk` CLI target (unlike turbogrep/turbosed below): expr.prn's
# expression language has no `print`/output primitive yet, so there is
# no real, honest CLI to build here -- see tests/test_awk.c's own
# header comment. This target is the real thing that exists today.
test-awk: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn \
		stdlib/regex/syntax.prn stdlib/regex/pcre.prn stdlib/expr.prn stdlib/awk.prn \
		-o tests/test_awk_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_awk.c runtime/parena_runtime.c \
		-o /tmp/test_awk_bin -lm
	/tmp/test_awk_bin

# test-webdriver -- real end-to-end verification for
# stdlib/net/webdriver.prn (the WebDriver-protocol "Selenium bindings"),
# same discipline as test-json/test-yaml above. Fully self-contained,
# no external process orchestration here: tests/test_webdriver.c itself
# starts and stops tests/fake_webdriver_server (a real Go binary
# standing in for a real WebDriver driver -- see that file's own header
# for the honest scope of what it stands in for) through real PARENA
# FFI (stdlib/process.prn's process-spawn/process-kill), not shell
# scripting around this Makefile target.
test-webdriver: build
	./parena build stdlib/string.prn stdlib/json.prn stdlib/net/tcp.prn stdlib/net/http.prn \
		stdlib/net/webdriver.prn stdlib/process.prn -o tests/test_webdriver_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_webdriver.c runtime/parena_runtime.c \
		-o /tmp/test_webdriver_bin -lm
	cd tests && go build -o fake_webdriver_server fake_webdriver_server.go
	cd tests && /tmp/test_webdriver_bin ./fake_webdriver_server 9515

# test-shell -- real end-to-end verification for stdlib/pty.prn and
# stdlib/shell.prn, the concrete "PARENA eats PITVIPER" dogfooding step
# (a real, direct port of PITVIPER's own internal/pty/pty_linux.go +
# pty_windows.go shell-resolution policy). Same discipline as
# test-json/test-yaml/test-awk above, but the strongest version of it in
# this file: actually forks a real bash process attached to a real pty,
# writes a real command, and reads real output back -- not a mock pty,
# not a stubbed subprocess.
test-shell: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn stdlib/pty.prn \
		stdlib/shell.prn -o tests/test_shell_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_shell.c runtime/parena_runtime.c \
		-o /tmp/test_shell_bin -lm
	/tmp/test_shell_bin

# test-sdl2 -- real end-to-end verification for stdlib/sdl2.prn, the
# first real slice of a PARENA-authored editor shell (founder: "continue
# working on parena editor"). Actually opens a real SDL2 window and
# renders real frames -- this box has no real X server, so a scratch
# Xvfb instance is launched on :97 for the duration of the test only
# (killed in a trap either way) rather than assuming a display is
# already up, same "make the target reproducible from a clean checkout"
# discipline test-webdriver's own fake_webdriver_server already follows.
test-sdl2: build
	./parena build stdlib/string.prn stdlib/sdl2.prn -o tests/test_sdl2_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_sdl2.c runtime/parena_runtime.c \
		-o /tmp/test_sdl2_bin -lSDL2 -lSDL2_ttf -lm
	@Xvfb :97 -screen 0 1280x720x24 & echo $$! > /tmp/test_sdl2_xvfb.pid; \
	trap 'kill $$(cat /tmp/test_sdl2_xvfb.pid) 2>/dev/null; rm -f /tmp/test_sdl2_xvfb.pid' EXIT; \
	sleep 1; \
	DISPLAY=:97 /tmp/test_sdl2_bin

# turbogrep -- real, standalone verification/benchmark CLI for
# stdlib/grep.prn (see docs/TURBOGREP_VERIFICATION_REPORT.md for the
# real corpus run this backs). tools/turbogrep_host.c is deliberately
# concatenated onto the end of the generated grep output rather than
# compiled as its own translation unit -- Parena's own generated
# structs (FileHandle/Engine/OpenMode/Vec/Arena/Result) have no
# emitted header yet, so a separate .c file would need hand-duplicated
# declarations with a real, silent ABI-mismatch risk if they ever
# drift; concatenation instead inherits the real, single, generated
# definitions directly, the same shape this session's own /tmp test
# harnesses already verified working. Not wired into the `parena` CLI
# itself the way `ci-status` is -- see EMILY/BACKLOG.md's own S170-295
# for why (turbo-sed/turbo-awk don't exist yet, and a system PATH swap
# is a separate, explicitly-approved step).
turbogrep: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn stdlib/regex/syntax.prn \
		stdlib/regex/pcre.prn stdlib/grep.prn -o /tmp/turbogrep_gen.c
	cat /tmp/turbogrep_gen.c tools/turbogrep_host.c > /tmp/turbogrep_full.c
	$(CC) -std=c99 -O2 -I runtime /tmp/turbogrep_full.c src/arena.c -o turbogrep

turbosed: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn stdlib/regex/syntax.prn \
		stdlib/regex/pcre.prn stdlib/sed.prn -o /tmp/turbosed_gen.c
	cat /tmp/turbosed_gen.c tools/turbosed_host.c > /tmp/turbosed_full.c
	$(CC) -std=c99 -O2 -I runtime /tmp/turbosed_full.c src/arena.c -o turbosed

tests/test_lexer_parser: tests/test_lexer_parser.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_lexer_parser tests/test_lexer_parser.c $(OBJ)

tests/test_region: tests/test_region.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_region tests/test_region.c $(OBJ)

tests/test_emit: tests/test_emit.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_emit tests/test_emit.c $(OBJ)

clean:
	rm -f parena .parena-bootstrap tests/test_lexer_parser tests/test_region tests/test_emit \
		src/*.o tools/ci_status_gen.c
