# F32 tensor substrate + conv kernels (stdlib/tensor/*.prn), the first PARENA-first dep of the PARENA-native TTS port
# (docs/TTS_VITS_PORT_PLAN.md). Strict flags, ASan+UBSan, -ffp-contract=off (bit-reproducible rounding).
.PHONY: test-tensor bench-tensor
test-tensor: build
	./parena build stdlib/tensor/f32.prn stdlib/tensor/conv.prn -o tests/test_tensor_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -ffp-contract=off -g -fsanitize=address,undefined -Iruntime -Itests \
		tests/test_tensor.c runtime/parena_runtime.c -o /tmp/test_tensor_bin -lm
	/tmp/test_tensor_bin

# PARENA-driven kernel vs hand-written C throughput (conv1d, VITS-decoder-shaped).
bench-tensor: build
	./parena build stdlib/tensor/f32.prn stdlib/tensor/conv.prn -o tests/test_tensor_gen.c
	gcc -std=c99 -O2 -ffp-contract=off -Iruntime -Itests tests/bench_tensor_conv.c runtime/parena_runtime.c -o /tmp/bench_tensor_bin -lm
	/tmp/bench_tensor_bin

.PHONY: test-tensor-nn
test-tensor-nn: build
	./parena build stdlib/tensor/f32.prn stdlib/tensor/nn.prn -o tests/test_tensor_nn_gen.c
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -ffp-contract=off -g -fsanitize=address,undefined -Iruntime -Itests \
		tests/test_tensor_nn.c runtime/parena_runtime.c -o /tmp/test_tensor_nn_bin -lm
	/tmp/test_tensor_nn_bin
