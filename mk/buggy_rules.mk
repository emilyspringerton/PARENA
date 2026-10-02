# SHANKPIT programmable buggy (stdlib/shankpit/buggy_rules.prn). Strict flags like every PARENA test.
.PHONY: test-buggy-rules
test-buggy-rules: build
	./parena build stdlib/shankpit/buggy_rules.prn -o tests/test_buggy_rules_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_buggy_rules.c runtime/parena_runtime.c -o /tmp/test_buggy_rules_bin -lm
	/tmp/test_buggy_rules_bin
