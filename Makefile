# Copyright (c) 2026 Christiaan (chris@boreddev.nl)
# BoredOS Core & Network Utilities Makefile

CC = x86_64-boredos-gcc

DESTDIR ?= $(abspath build/dist)

CFLAGS  = -Wall -Wextra -std=gnu11 -O2 -fno-stack-protector \
          -fno-stack-check -m64 -march=x86-64

LDFLAGS = -Wl,-z,max-page-size=0x1000 -Wl,-dynamic-linker,/usr/lib/ld.so -Wl,-rpath,/usr/lib:/lib -lm

# Complete list of standard and system status utilities
UTILS = clear echo grep cowsay sysfetch fdisk df du ps pwd rescan rev tail tar tty uname date \
	lsblk meminfo pci_list uptime beep reboot shutdown crash \
	math fbtest find head help hexdump kill mkfs_fat mkfs_ext4 loadkeys pidbench mixer audioplay \
	yawn service id sync mount umount vterm \
	getty login passwd useradd usermod userdel su doas \
	chmod chown whoami cat touch

APPS   = $(UTILS)
CONFS  = assets/sysfetch.cfg
ARTS   = assets/boredos.txt

AUTH_OBJS = obj/auth_subr.o obj/libcrypt_sha512.o

LWEXT4_DIR  = ../../fs/vendor/lwext4
LWEXT4_SRCS = $(wildcard $(LWEXT4_DIR)/src/*.c)
LWEXT4_OBJS = $(patsubst $(LWEXT4_DIR)/src/%.c, obj/lwext4/%.o, $(LWEXT4_SRCS))

LWEXT4_CFLAGS = $(CFLAGS) -Iinclude -I$(LWEXT4_DIR)/include -I$(LWEXT4_DIR)/include/misc \
                -include include/ext4_usr_config.h \
                -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable \
                -Wno-missing-field-initializers

all: $(APPS)

mkfs_ext4: obj/mkfs_ext4.o $(LWEXT4_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

getty: obj/getty.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

login: obj/login.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

passwd: obj/passwd.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

useradd: obj/useradd.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

usermod: obj/usermod.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

userdel: obj/userdel.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

su: obj/su.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

doas: obj/doas.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

id: obj/id.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

whoami: obj/whoami.o $(AUTH_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

%: obj/%.o
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
	cp $(APPS) $(DESTDIR)/bin/
	chmod 4755 $(DESTDIR)/bin/login
	chmod 4755 $(DESTDIR)/bin/passwd
	chmod 4755 $(DESTDIR)/bin/su
	chmod 4755 $(DESTDIR)/bin/doas
	mkdir -p $(DESTDIR)/Library/AppData/org.boredos.sysfetch
	cp $(CONFS) $(ARTS) $(DESTDIR)/Library/AppData/org.boredos.sysfetch/
	mkdir -p $(DESTDIR)/etc/skel/Library/AppData/org.boredos.sysfetch
	cp $(CONFS) $(DESTDIR)/etc/skel/Library/AppData/org.boredos.sysfetch/

clean:
	rm -rf obj build $(APPS)
