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

.PHONY: all build test test-domain4 test-domain5 test-multifile tools clean

all: build

build: parena

parena: src/main.c $(OBJ)
	$(CC) $(CFLAGS) -o parena src/main.c $(OBJ)

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

# tools/ci-status -- a real, native-PARENA CLI tool (stdlib/ci/status.prn),
# built to dogfood the language on a genuine, recurring need this repo's
# own workflow hits repeatedly (checking a GitHub Actions commit's own
# check-run status). Real two-step build, the honest reflection of how
# this actually works: (1) the already-built `parena` compiles the real
# PARENA module to C, (2) that generated C links against
# tools/ci_status_host.c's own real, hand-written host implementation
# (the #target FFI's actual host side, not left as a deferred gap here)
# plus tools/ci_status_main.c's own thin C entry point. This is the real,
# next step toward the founder's own stated "add it to the parena CLI"
# direction, not yet done -- this still builds a separate, standalone
# binary, not a `parena ci-status` subcommand.
tools: build
	./parena build stdlib/ci/status.prn -o tools/ci_status_gen.c
	$(CC) $(CFLAGS) -I runtime -I tools -include tools/ci_status.h \
		tools/ci_status_gen.c tools/ci_status_host.c tools/ci_status_main.c \
		-o tools/ci-status

clean:
	rm -f parena tests/test_lexer_parser tests/test_region tests/test_emit src/*.o \
		tools/ci_status_gen.c tools/ci-status
