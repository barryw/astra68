SUBDIRS := runtime metrics alloc storage vfs events input shell terminal \
	streams commands supervisor services

all test sanitize analyze clean:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done

.PHONY: all test sanitize analyze clean
