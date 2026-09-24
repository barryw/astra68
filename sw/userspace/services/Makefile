SUBDIRS := storage ramfs hostfs hostbench entropy events network ntpd posixd clipboard \
	terminal input display desktop remote-desktop media audio-certify pcm-certify

all test sanitize analyze clean:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done

.PHONY: all test sanitize analyze clean
