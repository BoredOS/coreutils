# Copyright (c) 2026 Christiaan (chris@boreddev.nl)
# BoredOS Core & Network Utilities Makefile

CC = x86_64-elf-gcc
LD = x86_64-elf-ld

# Smart SDK Resolution Logic
ifneq ($(BOREDOS_SDK),)
  ifeq ($(wildcard $(BOREDOS_SDK)/lib/libc.a),)
    BOOTSTRAP_SDK = $(BOREDOS_SDK)
    SDK_PATH      = $(BOREDOS_SDK)
  else
    SDK_PATH      = $(BOREDOS_SDK)
  endif
endif

# If SDK is still unresolved, fall back to a local standalone build folder
ifeq ($(SDK_PATH),)
  SDK_PATH = $(abspath build/sdk)
  ifeq ($(wildcard $(SDK_PATH)/lib/libc.a),)
    BOOTSTRAP_SDK = $(SDK_PATH)
  endif
endif

DESTDIR ?= $(abspath build/dist)

CFLAGS  = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector \
          -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone \
          -isystem $(SDK_PATH)/include

LDFLAGS = -m elf_x86_64 -nostdlib -static -no-pie -Ttext=0x40000000 \
          --no-dynamic-linker -z text -z max-page-size=0x1000 -e _start \
          -L$(SDK_PATH)/lib

# Complete list of standard and system status utilities
UTILS = clear echo grep cowsay sysfetch fdisk df du ps pwd rescan rev tail tar tty uname date \
	lsblk meminfo pci_list uptime beep reboot shutdown crash \
	math fbtest find head help hexdump kill mkfs_fat loadkeys pidbench mixer audioplay

ELFS   = $(patsubst %, %.elf, $(UTILS))
CONFS  = assets/sysfetch.cfg
ARTS   = assets/boredos.txt

all: $(ELFS)

%.elf: obj/%.o
	$(LD) $(LDFLAGS) $(SDK_PATH)/lib/crt0.o $(SDK_PATH)/lib/crti.o $< -lc $(SDK_PATH)/lib/crtn.o -o $@

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
