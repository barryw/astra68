SUBDIRS := runtime metrics alloc storage vfs events input shell terminal graphics interface messaging \
	streams posix commands supervisor services kits apps

all test sanitize analyze clean:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done

.PHONY: all test sanitize analyze clean
