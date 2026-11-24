# SHANKPIT action bar (stdlib/actionbar/*.prn). Strict flags like every PARENA test. Uses .parena-bootstrap
# (no SDL needed), same as test-shankpit460.
.PHONY: test-action-bar
test-action-bar: .parena-bootstrap
	./.parena-bootstrap build stdlib/actionbar/action_bar_rules.prn -o tests/test_action_bar_gen.c
	./.parena-bootstrap build stdlib/actionbar/example_plugin_mod.prn -o tests/test_action_bar_plugin_gen.c
	gcc -std=c99 -DPARENA_NO_GRAPHICS -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_action_bar.c runtime/parena_runtime.c -o /tmp/test_action_bar_bin -lm
	/tmp/test_action_bar_bin
