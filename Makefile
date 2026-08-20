# Bazel (MODULE.bazel, src/BUILD.bazel, tests/BUILD.bazel) is CI's and this
# repo's primary build system as of 2026-08-20 -- `bazel build //...` /
# `bazel test //tests:test_lexer_parser`. This Makefile is kept alongside
# for the fastest possible local edit-compile-test loop (no Bazel analysis
# overhead for a 5-file C project) — both stay in sync by hand since
# there's no codegen step between them yet.
CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -pedantic -g

SRC := src/arena.c src/ast.c src/lexer.c src/parser.c
OBJ := $(SRC:.c=.o)

.PHONY: all build test clean

all: build

build: parena

parena: src/main.c $(OBJ)
	$(CC) $(CFLAGS) -o parena src/main.c $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: tests/test_lexer_parser
	./tests/test_lexer_parser

tests/test_lexer_parser: tests/test_lexer_parser.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_lexer_parser tests/test_lexer_parser.c $(OBJ)

clean:
	rm -f parena tests/test_lexer_parser src/*.o
