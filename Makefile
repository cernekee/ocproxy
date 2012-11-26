DIR=contrib/ports/unix/proj/ocproxy

.PHONY: all install clean
all install clean:
	make -C $(DIR) $@

TAG=1.$(shell date --utc "+%Y%m%d%H%M%S")

.PHONY: dist
dist:
	git tag "$(TAG)"
	tar -czf "../ocproxy-$(TAG).tar.gz" --exclude=.git* --exclude=*~ *
