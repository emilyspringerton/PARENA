# SHANKPIT EduVM bindings (stdlib/shankpit/eduvm.prn, card #494). The generated C calls eduvm_host_*; the test
# supplies scripted stubs (tests/test_eduvm.c) so the PARENA logic is verified without the real VM.
.PHONY: test-eduvm
test-eduvm: build
	./parena build stdlib/shankpit/eduvm.prn -o tests/test_eduvm_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -Iruntime -Itests \
		tests/test_eduvm.c runtime/parena_runtime.c -o /tmp/test_eduvm_bin -lm
	/tmp/test_eduvm_bin
