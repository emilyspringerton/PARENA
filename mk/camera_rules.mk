# SHANKPIT broadcast cameras (stdlib/shankpit/camera_rules.prn). Strict flags like every PARENA test.
.PHONY: test-camera-rules
test-camera-rules: build
	./parena build stdlib/shankpit/camera_rules.prn -o tests/test_camera_rules_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_camera_rules.c runtime/parena_runtime.c -o /tmp/test_camera_rules_bin -lm
	/tmp/test_camera_rules_bin
