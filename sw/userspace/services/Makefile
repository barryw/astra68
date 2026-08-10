SUBDIRS := storage events terminal

all test sanitize analyze clean:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done

.PHONY: all test sanitize analyze clean
