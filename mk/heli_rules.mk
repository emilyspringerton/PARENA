# SHANKPIT programmable buggy (stdlib/shankpit/heli_rules.prn). Strict flags like every PARENA test.
.PHONY: test-heli-rules
test-heli-rules: build
	./parena build stdlib/shankpit/heli_rules.prn -o tests/test_heli_rules_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_heli_rules.c runtime/parena_runtime.c -o /tmp/test_heli_rules_bin -lm
	/tmp/test_heli_rules_bin
