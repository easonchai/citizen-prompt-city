CC      = clang
CFLAGS  = -std=c99 -Wall -Wextra -Werror -O1 -fno-common
WASMCC  = /opt/homebrew/opt/llvm/bin/clang
# wasm-ld ships in Homebrew's separate `lld` formula; put it on PATH for clang
WASMLD  = /opt/homebrew/opt/lld/bin
SRCS    = $(wildcard core/*.c)
HDRS    = $(wildcard core/*.h)
# export exactly the cz_* functions declared in citizen.h (plus memory, which wasm-ld exports by default)
LP     := (
EXPORTS = $(shell grep -oE 'cz_[a-z_]+\$(LP)' core/citizen.h | sort -u | sed 's/.$$//; s/^/-Wl,--export=/')

# bench and soak measure speed, so they build -O2 (the firmware is built -Os by Arduino)
OPTFLAGS = $(subst -O1,-O2,$(CFLAGS))

.PHONY: test wasm bench soak clean

test: tests/test_core
	@mkdir -p tests/out
	./tests/test_core

tests/test_core: $(SRCS) $(HDRS) tests/test_core.c
	$(CC) $(CFLAGS) -Icore -o $@ $(SRCS) tests/test_core.c

bench: tests/bench_core
	./tests/bench_core

tests/bench_core: $(SRCS) $(HDRS) tests/bench_core.c
	$(CC) $(OPTFLAGS) -Icore -o $@ $(SRCS) tests/bench_core.c

soak: tests/soak_core
	./tests/soak_core

tests/soak_core: $(SRCS) $(HDRS) tests/soak_core.c
	$(CC) $(OPTFLAGS) -Icore -o $@ $(SRCS) tests/soak_core.c

# launch-film screen sequences, rendered by the core itself (PRD 6.2)
wasm: sim/core.wasm

sim/core.wasm: $(SRCS) $(HDRS)
	PATH="$(WASMLD):$$PATH" $(WASMCC) --target=wasm32 -std=c99 -ffreestanding -nostdlib -O2 \
	  -DCZ_FREESTANDING -DNDEBUG -isystem core/freestanding -Wall -Wextra \
	  -Wl,--no-entry $(EXPORTS) -o $@ $(SRCS)

clean:
	rm -rf tests/test_core tests/bench_core tests/soak_core tests/out sim/core.wasm
