# SHANKPIT ambient-light slider rules (stdlib/shankpit/ambient_rules.prn). Strict flags like every PARENA test.
.PHONY: test-ambient-rules
test-ambient-rules: build
	./parena build stdlib/shankpit/ambient_rules.prn -o tests/test_ambient_rules_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_ambient_rules.c runtime/parena_runtime.c -o /tmp/test_ambient_rules_bin -lm
	/tmp/test_ambient_rules_bin
