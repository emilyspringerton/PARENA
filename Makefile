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

.PHONY: all build test test-domain4 test-domain5 test-multifile turbogrep clean

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
