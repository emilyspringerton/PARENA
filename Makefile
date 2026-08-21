# Bazel (MODULE.bazel, src/BUILD.bazel, tests/BUILD.bazel) is CI's and this
# repo's primary build system as of 2026-08-20 -- `bazel build //...` /
# `bazel test //tests:test_lexer_parser`. This Makefile is kept alongside
# for the fastest possible local edit-compile-test loop (no Bazel analysis
# overhead for a 5-file C project) — both stay in sync by hand since
# there's no codegen step between them yet.
CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -pedantic -g

SRC := src/arena.c src/ast.c src/lexer.c src/parser.c src/region.c src/emit.c
OBJ := $(SRC:.c=.o)

.PHONY: all build test test-domain4 test-domain5 test-multifile clean

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

tests/test_lexer_parser: tests/test_lexer_parser.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_lexer_parser tests/test_lexer_parser.c $(OBJ)

tests/test_region: tests/test_region.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_region tests/test_region.c $(OBJ)

tests/test_emit: tests/test_emit.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_emit tests/test_emit.c $(OBJ)

clean:
	rm -f parena .parena-bootstrap tests/test_lexer_parser tests/test_region tests/test_emit \
		src/*.o tools/ci_status_gen.c
