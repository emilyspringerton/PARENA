# BIG_O DAY-phase harvest/stain economy (stdlib/big_o/harvest_rules.prn). Strict flags like every PARENA test.
.PHONY: test-big-o-harvest
test-big-o-harvest: build
	./parena build stdlib/big_o/harvest_rules.prn -o tests/test_big_o_harvest_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -fsanitize=address,undefined -Iruntime -Itests \
		tests/test_big_o_harvest.c runtime/parena_runtime.c -o /tmp/test_big_o_harvest_bin -lm
	/tmp/test_big_o_harvest_bin
