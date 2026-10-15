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

.PHONY: test wasm clean

test: tests/test_core
	@mkdir -p tests/out
	./tests/test_core

tests/test_core: $(SRCS) $(HDRS) tests/test_core.c
	$(CC) $(CFLAGS) -Icore -o $@ $(SRCS) tests/test_core.c

wasm: sim/core.wasm

sim/core.wasm: $(SRCS) $(HDRS)
	PATH="$(WASMLD):$$PATH" $(WASMCC) --target=wasm32 -std=c99 -ffreestanding -nostdlib -O2 \
	  -DCZ_FREESTANDING -DNDEBUG -isystem core/freestanding -Wall -Wextra \
	  -Wl,--no-entry $(EXPORTS) -o $@ $(SRCS)

clean:
	rm -rf tests/test_core tests/out sim/core.wasm
