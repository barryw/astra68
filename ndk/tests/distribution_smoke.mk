ASTRA_NDK_ROOT ?= missing
BUILD_DIR ?= build/distribution-smoke

include $(ASTRA_NDK_ROOT)/make/astra-posix.mk

.PHONY: all
all: $(BUILD_DIR)/native.elf $(BUILD_DIR)/posix.elf \
	$(BUILD_DIR)/posix-cxx.elf $(BUILD_DIR)/native-static.elf \
	$(BUILD_DIR)/posix-static.elf $(BUILD_DIR)/posix-cxx-static.elf

$(BUILD_DIR)/native.elf: tests/distribution_native.c
	@mkdir -p $(@D)
	$(ASTRA_CC) $(ASTRA_CPPFLAGS) $(ASTRA_CFLAGS) $(ASTRA_LDFLAGS) \
		-o $@ $(ASTRA_CRT0) $< $(ASTRA_NATIVE_LIBS)
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed runtime.library.1 --needed compiler.library.1 $@

$(BUILD_DIR)/native-static.elf: tests/distribution_native.c
	@mkdir -p $(@D)
	$(ASTRA_CC) $(ASTRA_CPPFLAGS) $(ASTRA_CFLAGS) $(ASTRA_STATIC_LDFLAGS) \
		-o $@ $(ASTRA_STATIC_CRT0) $< $(ASTRA_STATIC_NATIVE_LIBS)
	@$(ASTRA_READELF) -hW $@ | grep -q 'Type:.*EXEC'
	@! $(ASTRA_READELF) -lW $@ | grep -q ' INTERP '
	@test "$$($(ASTRA_READELF) -dW $@ 2>/dev/null | \
		awk '/NEEDED/{n++} END{print n+0}')" = 0

$(BUILD_DIR)/posix-main.o: tests/distribution_posix.c
	@mkdir -p $(@D)
	$(ASTRA_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) -c $< -o $@

$(BUILD_DIR)/posix-program.o: $(ASTRA_POSIX_PROGRAM_SOURCE)
	@mkdir -p $(@D)
	$(ASTRA_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-DASTRA_POSIX_PROGRAM_NAME='"ndk-posix-contract"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=0 -DASTRA_POSIX_PROGRAM_MINOR=1 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra 68 Project"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra 68 Project"' \
		-c $< -o $@

$(BUILD_DIR)/posix.elf: $(BUILD_DIR)/posix-main.o \
		$(BUILD_DIR)/posix-program.o
	$(ASTRA_CC) $(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) $^ $(ASTRA_POSIX_LIBS)
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed libc.library.1 --needed runtime.library.1 \
		--needed compiler.library.1 $@

$(BUILD_DIR)/posix-static.elf: $(BUILD_DIR)/posix-main.o \
		$(BUILD_DIR)/posix-program.o
	$(ASTRA_CC) $(ASTRA_POSIX_CFLAGS) $(ASTRA_POSIX_STATIC_LDFLAGS) \
		-o $@ $(ASTRA_POSIX_STATIC_CRT0) $^ $(ASTRA_POSIX_STATIC_LIBS)
	@$(ASTRA_READELF) -hW $@ | grep -q 'Type:.*EXEC'
	@! $(ASTRA_READELF) -lW $@ | grep -q ' INTERP '
	@test "$$($(ASTRA_READELF) -dW $@ 2>/dev/null | \
		awk '/NEEDED/{n++} END{print n+0}')" = 0

$(BUILD_DIR)/posix-cxx-program.o: $(ASTRA_POSIX_PROGRAM_SOURCE)
	@mkdir -p $(@D)
	$(ASTRA_CC) $(ASTRA_POSIX_CPPFLAGS) $(ASTRA_POSIX_CFLAGS) \
		-DASTRA_POSIX_PROGRAM_NAME='"ndk-cxx-contract"' \
		-DASTRA_POSIX_PROGRAM_MAJOR=0 -DASTRA_POSIX_PROGRAM_MINOR=1 \
		-DASTRA_POSIX_PROGRAM_PATCH=0 \
		-DASTRA_POSIX_PROGRAM_AUTHOR='"Astra 68 Project"' \
		-DASTRA_POSIX_PROGRAM_COPYRIGHT='"Copyright 2026 Astra 68 Project"' \
		-c $< -o $@

$(BUILD_DIR)/posix-cxx.elf: ../sw/userspace/posix/tests/cxx_runtime.cpp \
		$(BUILD_DIR)/posix-cxx-program.o
	$(ASTRA_CXX) $(ASTRA_POSIX_CPPFLAGS) -std=c++17 \
		$(ASTRA_POSIX_CXXFLAGS) $(ASTRA_POSIX_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_CRT0) $(ASTRA_CXX_CRTBEGIN) $< \
		$(BUILD_DIR)/posix-cxx-program.o $(ASTRA_POSIX_CXX_LIBS) \
		$(ASTRA_CXX_CRTEND)
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed cxx.library.1 --needed libc.library.1 \
		--needed runtime.library.1 --needed unwind.library.1 \
		--needed compiler.library.1 $@

$(BUILD_DIR)/posix-cxx-static.elf: \
		../sw/userspace/posix/tests/cxx_runtime.cpp \
		$(BUILD_DIR)/posix-cxx-program.o
	$(ASTRA_CXX) $(ASTRA_POSIX_CPPFLAGS) -std=c++17 \
		$(ASTRA_POSIX_CXXFLAGS) $(ASTRA_POSIX_STATIC_LDFLAGS) -o $@ \
		$(ASTRA_POSIX_STATIC_CRT0) $(ASTRA_STATIC_CXX_CRTBEGIN) $< \
		$(BUILD_DIR)/posix-cxx-program.o $(ASTRA_POSIX_STATIC_CXX_LIBS) \
		$(ASTRA_STATIC_CXX_CRTEND)
	@$(ASTRA_READELF) -hW $@ | grep -q 'Type:.*EXEC'
	@! $(ASTRA_READELF) -lW $@ | grep -q ' INTERP '
	@test "$$($(ASTRA_READELF) -dW $@ 2>/dev/null | \
		awk '/NEEDED/{n++} END{print n+0}')" = 0
