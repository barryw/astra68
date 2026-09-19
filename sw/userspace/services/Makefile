SUBDIRS := storage hostfs hostbench entropy events network ntpd posixd clipboard \
	terminal input display desktop remote-desktop

all test sanitize analyze clean:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done

.PHONY: all test sanitize analyze clean
