# The portable ODFS core is private to the CDFS service, never to the NDK.
ODFS := ../../../third_party/odfs
CDFS_CPPFLAGS := $(PORT_CPPFLAGS) -I$(STORAGE)/include -I$(ODFS)/include \
	-I$(ODFS)/backends -DODFS_FEATURE_CDDA=0 -DODFS_FEATURE_LOG=0
ODFS_SOURCES := $(filter-out $(ODFS)/core/log.c, \
	$(wildcard $(ODFS)/core/*.c)) \
	$(wildcard $(ODFS)/backends/iso9660/*.c \
	$(ODFS)/backends/rock_ridge/*.c $(ODFS)/backends/joliet/*.c \
	$(ODFS)/backends/udf/*.c $(ODFS)/backends/hfs/*.c \
	$(ODFS)/backends/hfsplus/*.c)
CDFS_SOURCES := src/backends/cdfs.c src/backends/cdfs_media.c
CDFS_OBJECTS := $(patsubst $(ODFS)/%.c,build/m68k/cdfs/odfs/%.o,$(ODFS_SOURCES)) \
	$(patsubst src/%.c,build/m68k/cdfs/%.o,$(CDFS_SOURCES)) \
	build/m68k/cdfs/block_device.o
CDFS_HEADERS := include/astra/vfs_cdfs_backend.h \
	$(wildcard $(ODFS)/include/odfs/*.h $(ODFS)/backends/*/*.h)
CDFS_TEST_SOURCES := $(CDFS_SOURCES) $(ODFS_SOURCES) \
	$(STORAGE)/src/block_device.c $(STORAGE)/src/memory_block.c \
	src/vfs_service_core.c src/vfs_client.c src/vfs_local_transport.c \
	$(ASTRA_COMMON)/utf8.c tests/test_vfs_cdfs_backend.c

cdfs-backend: build/m68k/libastracdfs.a

build/m68k/libastracdfs.a: $(CDFS_OBJECTS) cdfs.mk
	$(ASTRA_REPLACE_ARCHIVE)

build/m68k/cdfs/odfs/%.o: $(ODFS)/%.c cdfs_port.h cdfs.mk
	@mkdir -p $(@D)
	$(CC) $(CDFS_CPPFLAGS) -std=gnu11 -Wall $(LIBRARY_FLAGS) \
		-include cdfs_port.h $(DEPFLAGS) -c $< -o $@

build/m68k/cdfs/%.o: src/%.c $(CDFS_HEADERS) cdfs_port.h cdfs.mk
	@mkdir -p $(@D)
	$(CC) $(CDFS_CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) \
		-include cdfs_port.h $(DEPFLAGS) -c $< -o $@

build/m68k/cdfs/block_device.o: $(STORAGE)/src/block_device.c cdfs.mk
	@mkdir -p $(@D)
	$(CC) $(CDFS_CPPFLAGS) $(COMMON_FLAGS) $(LIBRARY_FLAGS) \
		$(DEPFLAGS) -c $< -o $@

# Vendor sources retain their upstream warning policy. Adapter sources are
# additionally compiled with Astra's strict flags in the target/analyzer gate.
build/host/test_vfs_cdfs_backend: $(CDFS_TEST_SOURCES) $(HEADERS) $(CDFS_HEADERS) cdfs.mk
	@mkdir -p $(@D)
	$(HOST_CC) $(CDFS_CPPFLAGS) -std=gnu11 -Wall -Wextra $(HOST_FLAGS) \
		$(CDFS_TEST_SOURCES) -o $@

build/host/test_vfs_cdfs_backend_sanitize: $(CDFS_TEST_SOURCES) $(HEADERS) $(CDFS_HEADERS) cdfs.mk
	@mkdir -p $(@D)
	$(HOST_CC) $(CDFS_CPPFLAGS) -std=gnu11 -Wall -Wextra $(HOST_FLAGS) \
		$(SANITIZER_FLAGS) $(CDFS_TEST_SOURCES) -o $@

cdfs-test: build/host/test_vfs_cdfs_backend
	./$<

cdfs-sanitize: build/host/test_vfs_cdfs_backend_sanitize
	./$<

cdfs-analyze:
	@mkdir -p build/analyzer
	@for source in $(CDFS_SOURCES); do \
		$(ANALYZER_CC) $(CDFS_CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
			-fanalyzer -c $$source -o build/analyzer/$$(basename $$source .c).o || exit $$?; \
	done

test: cdfs-test
sanitize: cdfs-sanitize
analyze: cdfs-analyze
-include $(CDFS_OBJECTS:.o=.d)
.PHONY: cdfs-backend cdfs-test cdfs-sanitize cdfs-analyze
