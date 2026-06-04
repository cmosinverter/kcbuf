obj-m += ringbuf.o

# Kernel source/build tree to compile against.
KDIR ?= $(abspath $(CURDIR)/../linux)

PWD := $(CURDIR)

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

bench: bench.c
	$(CC) -static -O2 -Wall -pthread -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f bench

.PHONY: all modules bench clean
