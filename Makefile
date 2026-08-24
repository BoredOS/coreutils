# Copyright (c) 2026 Christiaan (chris@boreddev.nl)
# BoredOS Core & Network Utilities Makefile

CC = x86_64-boredos-gcc

DESTDIR ?= $(abspath build/dist)

CFLAGS  = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector \
          -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone

LDFLAGS = -static -no-pie -Wl,-Ttext=0x40000000 \
          -Wl,--no-dynamic-linker -Wl,-z,text -Wl,-z,max-page-size=0x1000

# Complete list of standard and system status utilities
UTILS = clear echo grep cowsay sysfetch fdisk df du ps pwd rescan rev tail tar tty uname date \
	lsblk meminfo pci_list uptime beep reboot shutdown crash \
	math fbtest find head help hexdump kill mkfs_fat mkfs_ext4 loadkeys pidbench mixer audioplay \
	job_applications

ELFS   = $(patsubst %, %.elf, $(UTILS))
CONFS  = assets/sysfetch.cfg
ARTS   = assets/boredos.txt

LWEXT4_DIR  = ../../fs/vendor/lwext4
LWEXT4_SRCS = $(wildcard $(LWEXT4_DIR)/src/*.c)
LWEXT4_OBJS = $(patsubst $(LWEXT4_DIR)/src/%.c, obj/lwext4/%.o, $(LWEXT4_SRCS))

LWEXT4_CFLAGS = $(CFLAGS) -Iinclude -I$(LWEXT4_DIR)/include -I$(LWEXT4_DIR)/include/misc \
                -include include/ext4_usr_config.h \
                -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable \
                -Wno-missing-field-initializers

all: $(ELFS)

mkfs_ext4.elf: obj/mkfs_ext4.o $(LWEXT4_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

%.elf: obj/%.o
	$(CC) $< $(LDFLAGS) -o $@

obj/lwext4/%.o: $(LWEXT4_DIR)/src/%.c
	@mkdir -p obj/lwext4
	$(CC) $(LWEXT4_CFLAGS) -c $< -o $@

obj/mkfs_ext4.o: src/mkfs_ext4.c
	@mkdir -p obj
	$(CC) $(LWEXT4_CFLAGS) -c $< -o $@

obj/%.o: src/%.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o $@

install: all
	mkdir -p $(DESTDIR)/bin
	cp $(ELFS) $(DESTDIR)/bin/
	mkdir -p $(DESTDIR)/Library/AppData/org.boredos.sysfetch
	cp $(CONFS) $(ARTS) $(DESTDIR)/Library/AppData/org.boredos.sysfetch/

clean:
	rm -rf obj build $(ELFS)
