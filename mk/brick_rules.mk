# SHANKPIT destructible brick decision logic (stdlib/shankpit/brick_rules.prn) composed with
# PAPERCRAFT's Paper Engine mods. Strict flags like every PARENA test.
.PHONY: test-brick-rules
test-brick-rules: build
	./parena build stdlib/papercraft/paper_fragment_mod.prn stdlib/papercraft/interact_falloff_mod.prn \
		stdlib/shankpit/brick_rules.prn -o tests/test_brick_rules_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_brick_rules.c runtime/parena_runtime.c -o /tmp/test_brick_rules_bin -lm
	/tmp/test_brick_rules_bin
