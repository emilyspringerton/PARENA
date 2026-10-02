# BIG_O LAB station rules (stdlib/big_o/lab_station_rules.prn, SECTION 592). Strict flags + ASan/UBSan, export coverage guard.
.PHONY: test-big-o-lab-station
test-big-o-lab-station: build
	./parena build stdlib/big_o/lab_station_rules.prn -o tests/test_big_o_lab_station_gen.c
	@missing=0; \
	for n in $$(awk '/^\(export/,/\)$$/' stdlib/big_o/lab_station_rules.prn | grep -v '^;;' | grep -o 'labst-[a-z0-9-]*' | sort -u); do \
		c=$$(echo $$n | tr - _); \
		if ! grep -q "$$c(" tests/test_big_o_lab_station.c; then echo "UNTESTED EXPORT: $$n"; missing=1; fi; \
	done; \
	[ $$missing -eq 0 ] && echo "coverage guard: every exported labst-* defn is called by the test"
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
		-Iruntime -Itests tests/test_big_o_lab_station.c runtime/parena_runtime.c -o /tmp/test_big_o_lab_station_bin -lm
	/tmp/test_big_o_lab_station_bin
