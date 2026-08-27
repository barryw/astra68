# Select an installed freestanding m68k toolchain while preserving CROSS=...
# supplied by callers and release automation.
ifeq ($(origin CROSS), undefined)
ifneq ($(shell command -v m68k-linux-gnu-gcc 2>/dev/null),)
CROSS = m68k-linux-gnu-
else ifneq ($(shell command -v m68k-elf-gcc 2>/dev/null),)
CROSS = m68k-elf-
else
CROSS = m68k-linux-gnu-
endif
endif

# Debug information for every m68k object. It costs nothing in what ships: the
# kernel is objcopy'd to a raw binary and the user image is stripped before it
# is packed, so both payloads are byte-identical with and without this. What it
# buys is a debugger that can name a function and a line instead of answering
# `?? ()`, which is the difference between diagnosing a fault and guessing at
# one. Set ASTRA_DEBUG_FLAGS= to build without it.
ASTRA_DEBUG_FLAGS ?= -g

# `ar r` leaves members that are no longer named by the build.  A renamed or
# removed object can therefore survive indefinitely and win symbol selection.
# Recreate archives off to the side, then publish the exact member set at once.
define ASTRA_REPLACE_ARCHIVE
	@mkdir -p $(@D)
	rm -f $@.tmp
	$(AR) rcs $@.tmp $^
	mv $@.tmp $@
endef
