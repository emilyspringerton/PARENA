# SHANKPIT programmable loot chests (stdlib/shankpit/chest_rules.prn). Strict flags like every PARENA test.
.PHONY: test-chest-rules
test-chest-rules: build
	./parena build stdlib/shankpit/chest_rules.prn -o tests/test_chest_rules_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_chest_rules.c runtime/parena_runtime.c -o /tmp/test_chest_rules_bin -lm
	/tmp/test_chest_rules_bin
