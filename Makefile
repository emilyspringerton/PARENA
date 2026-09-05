# Bazel (MODULE.bazel, src/BUILD.bazel, tests/BUILD.bazel) is CI's and this
# repo's primary build system as of 2026-08-20 -- `bazel build //...` /
# `bazel test //tests:test_lexer_parser`. This Makefile is kept alongside
# for the fastest possible local edit-compile-test loop (no Bazel analysis
# overhead for a 5-file C project) — both stay in sync by hand since
# there's no codegen step between them yet.
CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -pedantic -g

SRC := src/arena.c src/ast.c src/lexer.c src/parser.c src/region.c src/emit.c src/emit_ts.c src/emit_java.c src/fmt.c
OBJ := $(SRC:.c=.o)

.PHONY: all build test test-emit-ts test-emit-java test-base4 test-base4-vector test-base4-matrix test-base4-pattern test-mag-gematria test-papercraft-note-version test-datetime test-http-router test-http-routes test-http-controller test-process test-log-jsonl test-log-projector test-mixforge-import test-git test-ami test-bstree test-v16-lexer test-v16-parser test-sip-message test-sip-sdp test-sip-transaction test-net-proxy test-pentest-scan test-pentest-dot11 test-editor-document test-editor-registry test-domain4 test-domain5 test-multifile test-webdriver test-shell test-sdl2 test-editor test-editor-render test-editor-widget test-editor-spotlight test-construct-split test-textmate-loader test-editor-io test-editor-undo test-editor-indent test-editor-navigation test-selfhost-lexer test-selfhost-parser test-selfhost-region test-selfhost-emit test-selfhost-main test-selfhost-main-multifile editor-demo editor-demo-smoke turbogrep clean

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

# test-new -- real, automated verification for `parena new` (kanban priority-queue card
# PXCL-9311, the C-target sibling of BURROW's own real `burrow new`).
test-new: build
	bash tests/integration/run_new_check.sh

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

# test-base4 -- real end-to-end verification for stdlib/base4/algebra.prn, a real, faithful port
# of examples/engine.py.txt's own "CUSTOM BASE-4 / BINARY ALGEBRA LAB" (founder real-time: "add to
# parena stdlibs"). tests/test_base4_gen.c is committed, same real convention test_json_gen.c/
# test_selfhost_lexer_gen.c above already use.
test-base4: build
	./parena build stdlib/base4/algebra.prn -o tests/test_base4_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_base4.c \
		runtime/parena_runtime.c -o /tmp/test_base4_bin -lm
	/tmp/test_base4_bin

# test-base4-vector -- real end-to-end verification for stdlib/base4/vector.prn, the LO/GRAMMAR.md
# Phase 1 stdlib slice (founder real-time, 2026-08-30: "continue working on lo adding to the
# stdlib libs necessary to make the language actually function"). Confirms a real, live compiler
# bug in the same class linalg.prn's own header already documents (loop-accumulator int literals
# silently boxed as double) -- see vector.prn's own doc comment on `dot`.
test-base4-vector: build
	./parena build stdlib/base4/algebra.prn stdlib/base4/vector.prn -o tests/test_base4_vector_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_base4_vector.c \
		runtime/parena_runtime.c -o /tmp/test_base4_vector_bin -lm
	/tmp/test_base4_vector_bin

# test-base4-matrix -- real end-to-end verification for stdlib/base4/matrix.prn (S208-06, LO's
# real STACK/MATMUL stdlib target). Confirms two real, live compiler bugs: sibling same-named
# loop bindings colliding at C emission (worked around in matrix.prn itself), and matmul's own
# accumulator double-boxing (same class as base4/vector.prn's own documented `dot` bug) -- see
# matrix.prn's own doc comments.
test-base4-matrix: build
	./parena build stdlib/base4/algebra.prn stdlib/base4/matrix.prn -o tests/test_base4_matrix_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_base4_matrix.c \
		runtime/parena_runtime.c -o /tmp/test_base4_matrix_bin -lm
	/tmp/test_base4_matrix_bin

# test-base4-pattern -- real end-to-end verification for stdlib/base4/pattern.prn (S208-07, LO's
# PCRE-lite pattern matcher over base4 vectors). All real test LOGIC lives in pattern.prn's own
# `self-test`, written in pure PARENA (founder real-time: "write the MAIN in pure parena, at least
# like the ffi for it") -- tests/test_base4_pattern.c is the thinnest possible external C shim,
# calling self-test once and checking the one returned Bool.
test-base4-pattern: build
	./parena build stdlib/base4/pattern.prn -o tests/test_base4_pattern_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_base4_pattern.c \
		runtime/parena_runtime.c -o /tmp/test_base4_pattern_bin -lm
	/tmp/test_base4_pattern_bin

# test-mag-gematria -- real end-to-end verification for stdlib/mag/gematria.prn, the PARENA
# playground port of QUEENSALLYONLINEBOOKOFMAGIFICATIONANDUNICOR's squish/gematria pipeline.
test-mag-gematria: build
	./parena build stdlib/string.prn stdlib/mag/gematria.prn -o tests/test_mag_gematria_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_mag_gematria.c \
		runtime/parena_runtime.c -o /tmp/test_mag_gematria_bin -lm
	/tmp/test_mag_gematria_bin

# test-papercraft-note-version -- real end-to-end verification for
# stdlib/papercraft/note_version_mod.prn (iCloud-style version-management decision logic).
test-papercraft-note-version: build
	./parena build stdlib/papercraft/notes_mod.prn stdlib/papercraft/note_version_mod.prn \
		-o tests/test_papercraft_note_version_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests \
		tests/test_papercraft_note_version.c runtime/parena_runtime.c \
		-o /tmp/test_papercraft_note_version_bin -lm
	/tmp/test_papercraft_note_version_bin

# test-datetime -- real end-to-end verification for stdlib/datetime.prn (Go-style reference-time
# layout formatting). Confirms a real, live emit.c bug: fixed 1024-char buffers involved in
# emitting #target/inline-c bodies silently truncate longer strings -- see datetime.prn's own
# header comment.
test-datetime: build
	./parena build stdlib/string.prn stdlib/datetime.prn -o tests/test_datetime_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_datetime.c \
		runtime/parena_runtime.c -o /tmp/test_datetime_bin -lm
	/tmp/test_datetime_bin

# test-http-router -- real end-to-end verification for stdlib/http/router.prn (LO
# FRAMEWORK_NORTHSTAR.md's Phase B proof point). Confirms a real, genuine, previously-latent
# vec-string-at double-dereference bug (also fixed in regex/pcre.prn's own copy).
test-http-router: build
	./parena build stdlib/string.prn stdlib/http/router.prn -o tests/test_http_router_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_http_router.c \
		runtime/parena_runtime.c -o /tmp/test_http_router_bin -lm
	/tmp/test_http_router_bin

# test-http-routes -- real end-to-end verification for stdlib/http/routes.prn (LO
# FRAMEWORK_NORTHSTAR.md's own Rails-like "batteries included" plan, real HTTP-method +
# route-table layer on top of http/router.prn's own path-pattern matching).
test-http-routes: build
	./parena build stdlib/string.prn stdlib/http/router.prn stdlib/http/routes.prn -o tests/test_http_routes_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_http_routes.c \
		runtime/parena_runtime.c -o /tmp/test_http_routes_bin -lm
	/tmp/test_http_routes_bin

# test-http-controller -- real end-to-end verification for stdlib/http/controller.prn +
# examples/shithub_controller_demo.prn (LO FRAMEWORK_NORTHSTAR.md's own Rails-like "batteries
# included" plan, Controllers pillar, S225-04): the real Request/Response shape and a real,
# hand-written dispatch chain from match-route to the right demo action.
test-http-controller: build
	./parena build stdlib/string.prn stdlib/http/router.prn stdlib/http/routes.prn \
		stdlib/http/controller.prn examples/shithub_controller_demo.prn -o tests/test_http_controller_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_http_controller.c \
		runtime/parena_runtime.c -o /tmp/test_http_controller_bin -lm
	/tmp/test_http_controller_bin

# test-process -- real end-to-end verification for stdlib/process.prn's own run-capture/
# run-capture-exit-code (a real, general subprocess-capture primitive; SQL projectors below
# shell out through it to real DB CLI clients).
test-process: build
	./parena build stdlib/string.prn stdlib/process.prn -o tests/test_process_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_process.c \
		runtime/parena_runtime.c -o /tmp/test_process_bin -lm
	/tmp/test_process_bin

# test-log-jsonl -- real end-to-end verification for stdlib/log/event.prn + stdlib/log/jsonl.prn
# (LO FRAMEWORK_NORTHSTAR.md's own event-sourcing extension: the append-only JSONL log SQL
# projectors will replay). array.prn is a real, required sibling of io.prn (io.prn's own
# read-floats references NDArray) -- same real, pre-existing gap test-selfhost-main's own
# Makefile comment already documents.
test-log-jsonl: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn stdlib/json.prn \
		stdlib/log/event.prn stdlib/log/jsonl.prn -o tests/test_log_jsonl_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_log_jsonl.c \
		runtime/parena_runtime.c -o /tmp/test_log_jsonl_bin -lm
	/tmp/test_log_jsonl_bin

# test-log-projector -- real end-to-end verification for stdlib/log/projector.prn's own
# SQL-generation + real shell-invocation plumbing (SQLite/MySQL/PostgreSQL projectors over the
# JSONL event log). Real, honest, currently-unverified-live gap named in that file's own header
# comment: no real DB CLI/credentials exist in this sandbox, so this shims stand-in
# sqlite3/mysql scripts via a real, temporary PATH directory instead.
test-log-projector: build
	./parena build stdlib/string.prn stdlib/process.prn stdlib/log/event.prn stdlib/log/projector.prn \
		-o tests/test_log_projector_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_log_projector.c \
		runtime/parena_runtime.c -o /tmp/test_log_projector_bin -lm
	/tmp/test_log_projector_bin

# test-mixforge-import -- real end-to-end verification for stdlib/mixforge/import.prn (S243-01,
# MIXFORGE V0: paste-a-YouTube-URL track import). Real, temporary PATH directory shims a
# stand-in yt-dlp, same technique test-log-projector already established for sqlite3/mysql.
test-mixforge-import: build
	./parena build stdlib/string.prn stdlib/process.prn stdlib/log/event.prn stdlib/log/projector.prn \
		stdlib/mixforge/import.prn -o tests/test_mixforge_import_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_mixforge_import.c \
		runtime/parena_runtime.c -o /tmp/test_mixforge_import_bin -lm
	/tmp/test_mixforge_import_bin

# test-git -- real end-to-end verification for stdlib/git.prn (kanban cruise-queue card
# 342534534535: "copy the git interface paradigms and idoms and affordances from vs codes for
# GIT for our parena editor"). No stubbing needed -- a real `git` binary always exists in this
# sandbox, so this exercises every real function against an actual, freshly-created repo.
test-git: build
	./parena build stdlib/string.prn stdlib/process.prn stdlib/log/event.prn stdlib/log/projector.prn \
		stdlib/git.prn -o tests/test_git_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_git.c \
		runtime/parena_runtime.c -o /tmp/test_git_bin -lm
	/tmp/test_git_bin

# test-bstree -- real end-to-end verification for stdlib/bstree.prn (kanban priority-queue card
# "9933": a real, String-keyed ordered index -- insert/get/contains?, insert-or-update
# semantics, multi-key ordering, real misses).
test-bstree: build
	./parena build stdlib/string.prn stdlib/bstree.prn -o tests/test_bstree_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_bstree.c \
		runtime/parena_runtime.c -o /tmp/test_bstree_bin -lm
	/tmp/test_bstree_bin

# test-v16-lexer -- real end-to-end verification for stdlib/v16/lexer.prn (kanban priority-queue
# card 34134124, "parena v16 iteratejs engine" -- Phase 2a per PARENA/docs/V16_NORTHSTAR.md: a
# real, minimal JS tokenizer, the narrowest real slice past that doc's own scoping-only pass).
test-v16-lexer: build
	./parena build stdlib/string.prn stdlib/v16/lexer.prn -o tests/test_v16_lexer_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_v16_lexer.c \
		runtime/parena_runtime.c -o /tmp/test_v16_lexer_bin -lm
	/tmp/test_v16_lexer_bin

# test-v16-parser -- real end-to-end verification for stdlib/v16/parser.prn (V16 JS engine
# Phase 2b per PARENA/docs/V16_NORTHSTAR.md's own real phased plan -- the real JS expression
# parser built on top of Phase 2a's own lexer.prn output).
test-v16-parser: build
	./parena build stdlib/string.prn stdlib/v16/lexer.prn stdlib/v16/parser.prn -o tests/test_v16_parser_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_v16_parser.c \
		runtime/parena_runtime.c -o /tmp/test_v16_parser_bin -lm
	/tmp/test_v16_parser_bin

# test-sip-message -- real end-to-end verification for stdlib/sip/message.prn (kanban
# priority-queue card 3124213, "SIP phone primitives in parena stdlib"): real request/response
# parsing, header lookup, and a built request round-tripping back through the same parser.
test-sip-message: build
	./parena build stdlib/string.prn stdlib/sip/message.prn -o tests/test_sip_message_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_sip_message.c \
		runtime/parena_runtime.c -o /tmp/test_sip_message_bin -lm
	/tmp/test_sip_message_bin

# test-sip-sdp -- real end-to-end verification for stdlib/sip/sdp.prn (CarePyre SIP Phone
# Phase 2, kanban priority-queue card CAREPYRE-911343): real session-level + m=/a=rtpmap
# parsing, a real honest MissingMediaLine error, and a real built offer round-tripping back
# through the same parser.
test-sip-sdp: build
	./parena build stdlib/string.prn stdlib/sip/sdp.prn -o tests/test_sip_sdp_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_sip_sdp.c \
		runtime/parena_runtime.c -o /tmp/test_sip_sdp_bin -lm
	/tmp/test_sip_sdp_bin

# test-sip-transaction -- real end-to-end verification for stdlib/sip/transaction.prn (CarePyre
# SIP Phone Phase 3, kanban priority-queue card CAREPYRE-911343): real outbound/inbound call
# progressions and real, honest InvalidTransition errors for nonsensical event/state pairings.
test-sip-transaction: build
	./parena build stdlib/sip/transaction.prn -o tests/test_sip_transaction_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_sip_transaction.c \
		runtime/parena_runtime.c -o /tmp/test_sip_transaction_bin -lm
	/tmp/test_sip_transaction_bin

# test-net-proxy -- real end-to-end verification of stdlib/net/proxy.prn (kanban priority-queue
# card 434534, "use fatbaby proxy and proxy broker to inform vpn primatives built into parena"):
# real incoming-request parsing, real hop-by-hop header stripping (matching
# PRRJECT_FATBABY/broker/proxy.go's own exact list), and a real relay against a real local
# upstream HTTP server (tests/test_proxy_upstream_fixture.py).
test-net-proxy: build
	./parena build stdlib/string.prn stdlib/net/tcp.prn stdlib/net/http.prn stdlib/net/proxy.prn -o tests/test_net_proxy_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_net_proxy.c \
		runtime/parena_runtime.c -o /tmp/test_net_proxy_bin -lm
	/tmp/test_net_proxy_bin

# test-pentest-dot11 -- real end-to-end verification of stdlib/pentest/dot11.prn (KISMET_WIRELESS
# _NORTHSTAR.md's own real Phase 1: a native PARENA Radiotap+802.11 Beacon frame parser, kanban
# PENT-0011). Real, native PARENA (no FFI/host-glue needed, unlike pcap/scan above) -- tests
# against a hand-constructed, spec-accurate frame (see tests/test_pentest_dot11.c's own header
# comment for the real byte-offset-by-byte-offset layout and its own honest note on why no
# third-party dissector could cross-check it in this sandbox).
test-pentest-dot11: build
	./parena build stdlib/string.prn stdlib/pentest/dot11.prn -o tests/test_pentest_dot11_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_pentest_dot11.c \
		runtime/parena_runtime.c -o /tmp/test_pentest_dot11_bin -lm
	/tmp/test_pentest_dot11_bin

# test-wire -- stdlib/net/wire.prn 真實端對端驗證(kanban PCAP-0022)。
test-wire: build
	./parena build stdlib/string.prn stdlib/net/wire.prn -o tests/test_wire_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_wire.c \
		runtime/parena_runtime.c -o /tmp/test_wire_bin -lm
	/tmp/test_wire_bin

# test-editor-unicode -- real end-to-end verification of the UTF-8 codepoint-boundary fix in
# stdlib/editor/buffer.prn (kanban cruise-queue card 3454353, "fix unicode in parena editor").
test-editor-unicode: build
	./parena build stdlib/string.prn stdlib/editor/buffer.prn -o tests/test_editor_unicode_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_editor_unicode.c \
		runtime/parena_runtime.c -o /tmp/test_editor_unicode_bin -lm
	/tmp/test_editor_unicode_bin

# test-pentest-pcap -- real end-to-end verification for stdlib/pentest/pcap.prn's real host-side
# FFI glue (tools/pentest_pcap_host.c), kanban priority-queue card 435423, "parena PCAP
# primatives". Needs libpcap-dev installed (sudo-queue/47-install-libpcap-dev.sh, queued not yet
# run as of 2026-09-03 -- if that hasn't landed, `apt-get download libpcap0.8-dev && dpkg-deb -x
# <the .deb> /tmp/pcap_extract` gets the real headers without root, same real, live-verified path
# this target's own first pass used). tools/pentest_pcap_host.c is concatenated onto the
# generated output the same real way turbogrep_host.c/turbosed_host.c already are (Parena's own
# generated structs have no emitted header) -- but pentest_pcap's own host functions take a
# GENERATED struct (Capture) as a parameter, which ci_status.h's own `-include` forward-decl
# trick can't reach (a `-include`d header is textually prepended before parena_runtime.h itself,
# too early for a type the .prn file's own compile generates). Real, working fix instead: `sed`
# splices the 3 real forward declarations in right after parena's own auto-generated prototype
# block (anchored on the literal, always-generated `Result filter(Capture *, char *);` line),
# the one point in the file where Capture/Result/Option are already real and visible but no call
# site has fired yet.
test-pentest-pcap: build
	./parena build stdlib/string.prn stdlib/pentest/pcap.prn -o tests/test_pentest_pcap_gen.c
	sed -i '/^Result filter(Capture \*, char \*);$$/a\
Result pentest_pcap_start_capture(char *iface, Arena *dest);\
Option pentest_pcap_read_packet(Capture *cap, Arena *dest);\
int pentest_pcap_filter(Capture *cap, char *bpf_expr);' tests/test_pentest_pcap_gen.c
	cat tests/test_pentest_pcap_gen.c tools/pentest_pcap_host.c > /tmp/test_pentest_pcap_full.c
	cp /tmp/test_pentest_pcap_full.c tests/test_pentest_pcap_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_pentest_pcap.c \
		runtime/parena_runtime.c -o /tmp/test_pentest_pcap_bin -lpcap
	/tmp/test_pentest_pcap_bin

# test-pentest-scan -- real end-to-end verification for stdlib/pentest/scan.prn's real host-side
# FFI glue (tools/pentest_scan_host.c), kanban PEN-11412. Shells out to the real `nmap` binary
# (no library link needed, unlike pcap above) -- same real sed-splice trick as test-pentest-pcap
# for the same real reason (PARENA's own generated structs have no emitted header, so
# pentest_scan_host.c's own forward declaration has to land after parena's own generated
# prototype block, anchored on the always-generated `Result scan_ports(char *, int, Arena *);`
# line). Real target: 127.0.0.1 (authorized -- this box's own real infrastructure).
test-pentest-scan: build
	./parena build stdlib/string.prn stdlib/pentest/scan.prn -o tests/test_pentest_scan_gen.c
	sed -i '/^Result scan_ports(char \*, int, Arena \*);$$/a\
Result pentest_scan_ports(char *target, int profile, Arena *dest);' tests/test_pentest_scan_gen.c
	cat tests/test_pentest_scan_gen.c tools/pentest_scan_host.c > /tmp/test_pentest_scan_full.c
	cp /tmp/test_pentest_scan_full.c tests/test_pentest_scan_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_pentest_scan.c \
		runtime/parena_runtime.c -o /tmp/test_pentest_scan_bin
	/tmp/test_pentest_scan_bin

# test-editor-document -- real end-to-end verification for stdlib/editor/document.prn (real
# document management: editor/buffer.prn + papercraft/note_version_mod.prn tied together).
test-editor-document: build
	./parena build stdlib/string.prn stdlib/editor/buffer.prn stdlib/papercraft/note_version_mod.prn \
		stdlib/editor/document.prn -o tests/test_editor_document_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_editor_document.c \
		runtime/parena_runtime.c -o /tmp/test_editor_document_bin -lm
	/tmp/test_editor_document_bin

# test-editor-registry -- real end-to-end verification for stdlib/editor/registry.prn (S217-03:
# multi-document registry on top of editor/document.prn).
test-editor-registry: build
	./parena build stdlib/string.prn stdlib/editor/buffer.prn stdlib/papercraft/note_version_mod.prn \
		stdlib/editor/document.prn stdlib/editor/registry.prn -o tests/test_editor_registry_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_editor_registry.c \
		runtime/parena_runtime.c -o /tmp/test_editor_registry_bin -lm
	/tmp/test_editor_registry_bin

# test-textmate-loader -- real end-to-end verification for stdlib/editor/textmate_loader.prn
# (the real ".tmLanguage.json" grammar loader, "using text mate files" per founder real-time)
# and stdlib/editor/lang_json.prn (the first real language mod built on it) -- same discipline
# test-json above already establishes: not just "does it compile," actually loads a real
# grammar and tokenizes real source through it, checking real token scopes/spans.
test-textmate-loader: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn stdlib/regex/syntax.prn \
		stdlib/regex/pcre.prn stdlib/json.prn stdlib/editor/textmate.prn \
		stdlib/editor/textmate_loader.prn stdlib/editor/lang_json.prn \
		-o tests/test_textmate_loader_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_textmate_loader.c runtime/parena_runtime.c \
		-o /tmp/test_textmate_loader_bin -lm
	/tmp/test_textmate_loader_bin

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

# test-editor -- real end-to-end verification of the actual keyboard-
# driven text editing loop: stdlib/editor/buffer.prn (real text buffer +
# cursor) fed by stdlib/sdl2.prn's own real SDL_TEXTINPUT/SDL_KEYDOWN
# events (founder: "continue working on parena editor"). Drives a real
# edit sequence through SDL's own real event queue (SDL_PushEvent, tests/
# test_editor.c's own header comment has the full reasoning for why this
# is real input, not a mock) and confirms the buffer holds the real,
# correct final text.
test-editor: build
	./parena build stdlib/string.prn stdlib/sdl2.prn stdlib/editor/buffer.prn -o tests/test_editor_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_editor.c runtime/parena_runtime.c \
		-o /tmp/test_editor_bin -lSDL2 -lSDL2_ttf -lm
	@Xvfb :98 -screen 0 1280x720x24 & echo $$! > /tmp/test_editor_xvfb.pid; \
	trap 'kill $$(cat /tmp/test_editor_xvfb.pid) 2>/dev/null; rm -f /tmp/test_editor_xvfb.pid' EXIT; \
	sleep 1; \
	DISPLAY=:98 /tmp/test_editor_bin

# test-editor-render -- real end-to-end verification tying together
# everything shipped this session: the tokenizer, the real PARENA
# grammar, a real theme (scope -> color), and real rendering -- the
# concrete "see real syntax-highlighted PARENA source on screen" moment
# (founder: "start adding all of the features of textmate" -> "ALL THE
# FEATURES").
test-editor-render: build
	./parena build stdlib/string.prn stdlib/regex/syntax.prn stdlib/regex/pcre.prn stdlib/sdl2.prn \
		stdlib/editor/textmate.prn stdlib/editor/textmate_parena.prn stdlib/editor/textmate_markdown.prn \
		stdlib/editor/theme.prn stdlib/editor/render.prn -o tests/test_editor_render_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_editor_render.c runtime/parena_runtime.c \
		-o /tmp/test_editor_render_bin -lSDL2 -lSDL2_ttf -lm
	@Xvfb :96 -screen 0 1280x720x24 & echo $$! > /tmp/test_editor_render_xvfb.pid; \
	trap 'kill $$(cat /tmp/test_editor_render_xvfb.pid) 2>/dev/null; rm -f /tmp/test_editor_render_xvfb.pid' EXIT; \
	sleep 1; \
	DISPLAY=:96 /tmp/test_editor_render_bin

# test-editor-widget -- real end-to-end verification of stdlib/editor/
# widget.prn's Toggle type, the first real slice of the "UI widget
# system" (2026-08-27, founder real-time, chosen as the next thread
# after v0.77.0-v0.80.0 shipped).
test-editor-widget: build
	./parena build stdlib/string.prn stdlib/sdl2.prn stdlib/editor/widget.prn -o tests/test_editor_widget_gen.c
	$(CC) -std=c99 -Wall -Wextra -I runtime -I tests tests/test_editor_widget.c runtime/parena_runtime.c \
		-o /tmp/test_editor_widget_bin -lSDL2 -lSDL2_ttf -lm
	@Xvfb :98 -screen 0 1280x720x24 & echo $$! > /tmp/test_editor_widget_xvfb.pid; \
	trap 'kill $$(cat /tmp/test_editor_widget_xvfb.pid) 2>/dev/null; rm -f /tmp/test_editor_widget_xvfb.pid' EXIT; \
	sleep 1; \
	DISPLAY=:98 /tmp/test_editor_widget_bin

# test-editor-spotlight -- real end-to-end verification of stdlib/
# editor/spotlight.prn, the PARENA editor's own Ctrl+T/Cmd+T command
# palette (2026-08-27, founder real-time: "quick open via ctrl+t...
# thats going to be a magic spotlight feature"). Pure logic, no SDL2/
# Xvfb needed -- see tests/test_editor_spotlight.c's own header
# comment for the real reasoning.
test-editor-spotlight: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn stdlib/expr.prn \
		stdlib/editor/construct_split.prn stdlib/editor/spotlight.prn -o tests/test_editor_spotlight_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_editor_spotlight.c \
		runtime/parena_runtime.c -o /tmp/test_editor_spotlight_bin -lm
	/tmp/test_editor_spotlight_bin

# test-construct-split -- real, dedicated verification of stdlib/
# editor/construct_split.prn's own splitting algorithm, independent of
# the Spotlight overlay it plugs into (2026-08-28, founder real-time:
# "i want this as a parena mod... if i type /construct-split 10 if it
# is a construct file it should use file start and file end to open
# up new panes with the chunks of the file broken into roughly equal
# 10 sizes its not gonna be totally equal"). Pure logic, no SDL2/Xvfb
# needed -- see tests/test_construct_split.c's own header comment.
test-construct-split: build
	./parena build stdlib/string.prn stdlib/array.prn \
		stdlib/editor/construct_split.prn -o tests/test_construct_split_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_construct_split.c \
		runtime/parena_runtime.c -o /tmp/test_construct_split_bin -lm
	/tmp/test_construct_split_bin

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

# editor-demo -- the real, standalone, runnable PARENA editor (founder:
# "continue working on parena editor"): a real SDL2 window, a real
# interactive event loop, real keyboard-driven single-line text editing
# with real live syntax highlighting -- every real piece shipped this
# session (buffer, tokenizer, grammar, theme, renderer) tied together
# into an actual program a real person can run and type into. Same real
# "host driver concatenated onto the generated .c" shape as turbogrep/
# turbosed above, but links runtime/parena_runtime.c (not src/arena.c)
# -- this program needs the real SDL2/SDL2_ttf host glue turbogrep/
# turbosed don't.
# PRNFMT_RENAME -- real, standard C symbol-renaming (the same technique
# real, large C codebases use for exactly this, e.g. zlib's own
# "prefix all symbols" build option): src/arena.c's own arena_init/
# arena_alloc/arena_strdup/arena_free_all collide at LINK time (not
# just the real, separately-solved Arena TYPE collision runtime/
# prnfmt_bridge.c's own header comment documents) with runtime/
# parena_runtime.c's own identically-named functions once both land in
# the SAME final editor-demo executable -- confirmed live via a real
# "multiple definition of `arena_init`" linker error, not assumed.
# Fixed by recompiling src/arena.c + src/fmt.c FRESH for this one link
# (not reusing src/arena.o/src/fmt.o, which are compiled for the real
# `parena` compiler's own, separate linkage) with these 4 symbols
# preprocessor-renamed -- applied consistently to src/arena.c, src/
# fmt.c (which itself calls arena_alloc/arena_strdup internally), and
# runtime/prnfmt_bridge.c (which also calls them), so every reference
# resolves to the same real, renamed symbol.
PRNFMT_RENAME := -Darena_init=pf_arena_init -Darena_alloc=pf_arena_alloc \
	-Darena_strdup=pf_arena_strdup -Darena_free_all=pf_arena_free_all

# editor-demo's own real source-file list lives in examples/editor-demo-sources.txt, NOT
# hardcoded here -- real, confirmed-live recurring bug (2026-08-28, the 2nd+ real instance in
# THIS repo alone: pty.prn/shell.prn were missed here once already this session, now json.prn/
# textmate_loader.prn/lang_json.prn hit the SAME class in CI's own separate hardcoded copy of
# this exact list, .github/workflows/ci.yml's "Generate editor-demo C source" step). CI now
# reads the same file, so this list only needs updating in ONE real place going forward.
editor-demo: build
	./parena build $(shell tr '\n' ' ' < examples/editor-demo-sources.txt) -o /tmp/editor_demo_gen.c
	cat /tmp/editor_demo_gen.c examples/editor_main.c > /tmp/editor_demo_full.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror $(PRNFMT_RENAME) -c src/arena.c -o /tmp/pf_arena.o
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror $(PRNFMT_RENAME) -c src/fmt.c -o /tmp/pf_fmt.o
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror $(PRNFMT_RENAME) -I runtime -c runtime/prnfmt_bridge.c -o /tmp/pf_bridge.o
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime /tmp/editor_demo_full.c runtime/parena_runtime.c \
		/tmp/pf_bridge.o /tmp/pf_arena.o /tmp/pf_fmt.o \
		-o editor-demo -lSDL2 -lSDL2_ttf -lm

# test-editor-io -- real end-to-end verification of the editor's own
# real save/load path (stdlib/io.prn's file-open/write-string/
# read-line/file-close + editor/buffer.prn's from-text), the same real
# functions examples/editor_main.c's own save_to_file/load_first_line
# helpers call.
test-editor-io: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn stdlib/editor/buffer.prn \
		-o tests/test_editor_io_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_editor_io.c \
		runtime/parena_runtime.c -o /tmp/test_editor_io_bin -lm
	/tmp/test_editor_io_bin

# test-selfhost-lexer -- real end-to-end verification of
# selfhost/lexer.prn, the first real slice of PARENA's own
# self-hosting effort (NORTHSTAR.md's own "Self-hosting" section).
test-selfhost-lexer: build
	./parena build stdlib/string.prn selfhost/lexer.prn -o tests/test_selfhost_lexer_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_selfhost_lexer.c \
		runtime/parena_runtime.c -o /tmp/test_selfhost_lexer_bin -lm
	/tmp/test_selfhost_lexer_bin

# test-selfhost-parser -- real end-to-end verification of
# selfhost/parser.prn, the real second domain of PARENA's own
# self-hosting effort.
test-selfhost-parser: build
	./parena build stdlib/string.prn selfhost/lexer.prn selfhost/parser.prn -o tests/test_selfhost_parser_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_selfhost_parser.c \
		runtime/parena_runtime.c -o /tmp/test_selfhost_parser_bin -lm
	/tmp/test_selfhost_parser_bin

# test-selfhost-region -- real end-to-end verification of
# selfhost/region.prn, the real third domain of PARENA's own
# self-hosting effort.
test-selfhost-region: build
	./parena build stdlib/string.prn selfhost/lexer.prn selfhost/parser.prn selfhost/region.prn \
		-o tests/test_selfhost_region_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_selfhost_region.c \
		runtime/parena_runtime.c -o /tmp/test_selfhost_region_bin -lm
	/tmp/test_selfhost_region_bin

# test-selfhost-emit -- real end-to-end verification of
# selfhost/emit.prn, the real fourth domain of PARENA's own
# self-hosting effort. Real compile+run, not just in-process
# assertions -- see that test's own header comment.
test-selfhost-emit: build
	./parena build stdlib/string.prn selfhost/lexer.prn selfhost/parser.prn selfhost/emit.prn \
		-o tests/test_selfhost_emit_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_selfhost_emit.c \
		runtime/parena_runtime.c -o /tmp/test_selfhost_emit_bin -lm
	/tmp/test_selfhost_emit_bin

# test-selfhost-main -- real end-to-end verification of selfhost/
# main.prn's own build-file, the real fifth domain of PARENA's own
# self-hosting effort (the CLI-runner analog to src/main.c's own
# cmd_build): a real disk-to-disk pipeline function, not just an
# in-memory one. Needs stdlib/array.prn compiled in alongside
# stdlib/io.prn (io.prn's own read-floats references NDArray, which
# array.prn defines -- a real, pre-existing gap in io.prn only
# surfaced once something finally compiles it standalone with its own
# full sibling set).
test-selfhost-main: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn selfhost/lexer.prn \
		selfhost/parser.prn selfhost/region.prn selfhost/emit.prn selfhost/main.prn \
		-o tests/test_selfhost_main_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_selfhost_main.c \
		runtime/parena_runtime.c -o /tmp/test_selfhost_main_bin -lm
	/tmp/test_selfhost_main_bin

# test-selfhost-main-multifile -- real end-to-end verification of
# selfhost/main.prn's own build-files (2026-08-28, "continue working on
# parena self hosted" / "removing c ffi when possible"), the multi-file
# companion to build-file above: parses EACH input path, merges every
# file's top-level children into ONE combined unit, region-analyzes and
# emits it exactly once -- the real "multiple files are combined into
# one compilation unit" behavior `parena build a.prn b.prn -o out.c`'s
# own usage line documents, reproduced through the selfhost pipeline.
test-selfhost-main-multifile: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn selfhost/lexer.prn \
		selfhost/parser.prn selfhost/region.prn selfhost/emit.prn selfhost/main.prn \
		-o tests/test_selfhost_main_multifile_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_selfhost_main_multifile.c \
		runtime/parena_runtime.c -o /tmp/test_selfhost_main_multifile_bin -lm
	/tmp/test_selfhost_main_multifile_bin

# parena-selfhost -- the real, standalone, argv-parsing self-hosted compiler
# binary (2026-09-02, "continue working on parena self hosted compiler"),
# closing the honest gap selfhost/main.prn's own header comment names:
# "NOT yet a real argv-parsing standalone executable". selfhost/cli_main.c
# is the thin, hand-written C OS-interop layer (argv -> a real Vec String,
# an exit code back out) -- same real "small C driver calling into
# compiled PARENA logic" shape every selfhost test harness already
# established, just a real, permanent binary instead of a test-only one.
# All real compiler logic (parse/region-analyze/emit/write) still lives
# entirely in selfhost/*.prn, unchanged.
parena-selfhost: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/io.prn selfhost/lexer.prn \
		selfhost/parser.prn selfhost/region.prn selfhost/emit.prn selfhost/main.prn \
		-o selfhost/selfhost_cli_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime selfhost/cli_main.c \
		runtime/parena_runtime.c -o parena-selfhost -lm

# test-selfhost-cli -- real end-to-end verification of parena-selfhost
# itself (not a driver calling build-file/build-files in-process, an
# actual `fork`+`exec` of the real compiled binary with real argv) --
# proves the whole real chain: argv parsing, single- and multi-file
# build, a real nonzero exit code + real stderr message on a real
# failure (a nonexistent input path), matching src/main.c's own real
# CLI contract, not just that build-file/build-files themselves work
# (already proven by test-selfhost-main/-multifile above).
test-selfhost-cli: parena-selfhost
	./parena-selfhost build examples/valid_only.prn -o /tmp/selfhost_cli_single.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o /tmp/selfhost_cli_single_bin \
		tests/integration/driver_valid_only.c /tmp/selfhost_cli_single.c runtime/parena_runtime.c
	/tmp/selfhost_cli_single_bin && echo "PASS: parena-selfhost build (single file) -> real, compiled, correctly-running output"
	./parena-selfhost build examples/selfhost_multifile_a.prn examples/selfhost_multifile_b.prn -o /tmp/selfhost_cli_multi.c
	grep -q get_message /tmp/selfhost_cli_multi.c && grep -q use_message /tmp/selfhost_cli_multi.c && echo "PASS: parena-selfhost build (multi-file) produced real output containing both real functions"
	./parena-selfhost build examples/this_file_does_not_exist.prn -o /tmp/selfhost_cli_fail.c; \
		test $$? -ne 0 && echo "PASS: parena-selfhost exits nonzero on a real, honest failure"
	@echo "ALL PASS"

# test-editor-undo -- real, direct verification of the Ctrl+Z undo
# stack semantics (push/pop/overflow), the same real logic
# examples/editor_main.c's own push_undo/pop_undo use.
test-editor-undo: build
	./parena build stdlib/string.prn stdlib/array.prn stdlib/editor/buffer.prn \
		-o tests/test_editor_undo_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_editor_undo.c \
		runtime/parena_runtime.c -o /tmp/test_editor_undo_bin -lm
	/tmp/test_editor_undo_bin

# test-editor-indent -- real, direct verification of paren_depth_before,
# examples/editor_main.c's own real auto-indent-on-Enter bracket-
# nesting counter. Pure C string logic, no PARENA/SDL dependency at
# all -- no `./parena build` step needed.
test-editor-indent:
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -o /tmp/test_editor_indent_bin tests/test_editor_indent.c
	/tmp/test_editor_indent_bin

# test-editor-navigation -- real, direct verification of parent_dir_of,
# the file-tree sidebar's own real directory-navigation helper
# (2026-08-27, closing v0.83.0's own honest "clicking a subdirectory
# opens it as a file" gap).
test-editor-navigation:
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -o /tmp/test_editor_navigation_bin tests/test_editor_navigation.c
	/tmp/test_editor_navigation_bin

# editor-demo-smoke -- real, bounded build-verification run for
# editor-demo on this repo's own headless dev box (a real, scratch Xvfb
# instance, killed either way) -- confirms the real binary launches and
# runs its real event loop without crashing. NOT a substitute for
# actually running `./editor-demo` on a real machine with a real screen
# and a real keyboard -- that's the actual deliverable this target only
# checks doesn't crash on startup.
editor-demo-smoke: editor-demo
	@Xvfb :95 -screen 0 1280x720x24 & echo $$! > /tmp/editor_demo_xvfb.pid; \
	trap 'kill $$(cat /tmp/editor_demo_xvfb.pid) 2>/dev/null; rm -f /tmp/editor_demo_xvfb.pid' EXIT; \
	sleep 1; \
	DISPLAY=:95 timeout 2 ./editor-demo; \
	code=$$?; \
	if [ $$code -ne 0 ] && [ $$code -ne 124 ]; then echo "editor-demo exited abnormally: $$code"; exit 1; fi; \
	echo "editor-demo ran its real event loop without crashing"

tests/test_lexer_parser: tests/test_lexer_parser.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_lexer_parser tests/test_lexer_parser.c $(OBJ)

tests/test_region: tests/test_region.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_region tests/test_region.c $(OBJ)

tests/test_emit: tests/test_emit.c $(OBJ)
	$(CC) $(CFLAGS) -o tests/test_emit tests/test_emit.c $(OBJ)

test-emit-ts: tests/test_emit_ts.c $(OBJ)
	$(CC) $(CFLAGS) -Werror -o tests/test_emit_ts tests/test_emit_ts.c $(OBJ)
	./tests/test_emit_ts

test-emit-java: tests/test_emit_java.c $(OBJ)
	$(CC) $(CFLAGS) -Werror -o tests/test_emit_java tests/test_emit_java.c $(OBJ)
	./tests/test_emit_java

clean:
	rm -f parena .parena-bootstrap tests/test_lexer_parser tests/test_region tests/test_emit \
		tests/test_emit_ts tests/test_emit_java src/*.o tools/ci_status_gen.c

# test-rtp -- real end-to-end verification for stdlib/sip/rtp.prn (kanban priority-queue card
# PBX-001, "narrow scope parena PBX primitives... close to the metal like what does asterisk need").
test-rtp: build
	./parena build stdlib/string.prn stdlib/net/wire.prn stdlib/sip/rtp.prn -o tests/test_rtp_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_rtp.c \
		runtime/parena_runtime.c -o /tmp/test_rtp_bin -lm
	/tmp/test_rtp_bin

# test-ami -- real end-to-end verification for stdlib/pbx/ami.prn (kanban cruise-queue card
# PBX-003, Phase 1 of PBX_ASTERISK_NORTHSTAR.md's own plan). Real, honest boundary: exercises
# the real wire-format parsing/building against real AMI message samples -- a real login round
# trip against a live Asterisk instance is Phase 2, untestable in this sandbox.
test-ami: build
	./parena build stdlib/string.prn stdlib/pbx/ami.prn -o tests/test_ami_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_ami.c \
		runtime/parena_runtime.c -o /tmp/test_ami_bin -lm
	/tmp/test_ami_bin

# test-dns -- real end-to-end verification for stdlib/net/dns.prn (kanban priority-queue card
# 334534, "DNS primitives parena").
test-dns: build
	./parena build stdlib/string.prn stdlib/net/wire.prn stdlib/net/dns.prn -o tests/test_dns_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_dns.c \
		runtime/parena_runtime.c -o /tmp/test_dns_bin -lm
	/tmp/test_dns_bin

# test-ber -- real end-to-end verification for stdlib/ldap/ber.prn (kanban priority-queue card
# 342332432423, "ldap primatives parena").
test-ber: build
	./parena build stdlib/string.prn stdlib/net/wire.prn stdlib/ldap/ber.prn -o tests/test_ber_gen.c
	$(CC) -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -I tests tests/test_ber.c \
		runtime/parena_runtime.c -o /tmp/test_ber_bin -lm
	/tmp/test_ber_bin
