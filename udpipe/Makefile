all: udpipe

.PHONY: udpipe

udpipe:
	$(MAKE) -C udt/
	$(MAKE) -C src/

clean:
	$(MAKE) clean -C src/
	$(MAKE) clean -C udt/
