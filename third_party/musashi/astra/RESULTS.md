# PMMU verification results

Verified on 2026-07-13 on `nuc` with GCC 13.3.0.

| Check | Result |
|---|---|
| Clean Musashi build and focused suite | Pass |
| Standalone PMMU conformance groups | 15/15 pass |
| Encoded Musashi/PMMU integration groups | 13/13 pass |
| Focused suite with AddressSanitizer + UndefinedBehaviorSanitizer | Pass |
| Standalone model with GCC `-fanalyzer` and `-Werror` | Pass |
| Complete focused suite with GCC `-fanalyzer` | Pass |
| Vendored Musashi example build | Pass |
| Existing Musashi instruction binaries | 79/79 pass (60 MC68000, 19 MC68040) |

Focused command:

```sh
cd third_party/musashi
make clean test-pmmu
```

Sanitizer command:

```sh
make clean test-pmmu \
  CFLAGS='-O1 -g -std=c11 -Wall -Wextra -pedantic \
  -fno-omit-frame-pointer -fsanitize=address,undefined'
```

The existing instruction binaries were assembled on `nuc` with its installed
GNU m68k cross-tools:

```sh
make build_tests M68K_AS=m68k-linux-gnu-as M68K_LD=m68k-linux-gnu-ld
make test
```

The format-A/B PMMU frame and normal page-fault restart blocker is closed for
the tested instruction classes. The narrower external-BERR, last-write
classification, nested-handler-fault, cache, RMC, and timing gaps remain listed
in `README.md` and are not behavior for Astra OS to assume.
