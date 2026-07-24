.PHONY: build check clean fmt kernel performance post run soak test toolchain

RUSTUP_TOOLCHAIN ?= 1.97.0
CARGO := rustup run $(RUSTUP_TOOLCHAIN) cargo
ROM ?= ../sw/boot/astra_boot.bin
SOAK_CYCLES ?= 1
SOAK_MAX_CYCLES ?=
PERFORMANCE_SOAK_CYCLES ?= 1000
PERFORMANCE_SOAK_MAX_CYCLES ?= 675000000

toolchain:
	rustup toolchain install $(RUSTUP_TOOLCHAIN) --profile minimal --component rustfmt --component clippy

build:
	$(CARGO) build --release --workspace

check:
	$(CARGO) clippy --workspace --all-targets -- -D warnings

clean:
	$(CARGO) clean

fmt:
	$(CARGO) fmt --all -- --check

post:
	$(CARGO) run -p astravm-machine --example boot

kernel:
	$(CARGO) run -p astravm-machine --example kernel -- $(ROM)

soak:
	$(CARGO) run --release -p astravm-machine --example soak -- \
		$(ROM) $(SOAK_CYCLES) $(SOAK_MAX_CYCLES)

performance:
	$(CARGO) run --release -p astravm-machine --example soak -- \
		$(ROM) $(PERFORMANCE_SOAK_CYCLES) $(PERFORMANCE_SOAK_MAX_CYCLES)

run:
	$(CARGO) run --release -p astravm-app

test:
	$(CARGO) test --workspace
	$(MAKE) -C ../conformance test
