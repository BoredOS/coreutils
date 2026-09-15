// Copyright (c) 2026 zeyadhost (https://github.com/zeyadhost)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>

static int print_mounts_proc(void) {
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char dev[128] = {0};
        char mnt[128] = {0};
        char type[64] = {0};
        char opts[64] = {0};
        int fields = sscanf(line, "%127s %127s %63s %63s", dev, mnt, type, opts);
        if (fields >= 3) {
            if (fields >= 4 && opts[0]) {
                printf("%s on %s type %s (%s)\n", dev, mnt, type, opts);
            } else {
                printf("%s on %s type %s\n", dev, mnt, type);
            }
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    const char *dev = NULL;
    const char *target = NULL;
    const char *fstype = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            fstype = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            i++; // skip options argument
        } else if (argv[i][0] == '-') {
            continue; // skip other flags
        } else if (!dev) {
            dev = argv[i];
        } else if (!target) {
            target = argv[i];
        }
    }

    if (!dev && !target) {
        if (print_mounts_proc() != 0) {
            fprintf(stderr, "mount: failed to read /proc/mounts\n");
            return 1;
        }
        return 0;
    }

    if (!dev || !target) {
        fprintf(stderr, "usage: %s [-t fstype] <device> <mountpoint>\n", argv[0]);
        return 1;
    }

    if (getuid() != 0 && geteuid() != 0) {
        fprintf(stderr, "mount: Permission denied (only root can mount filesystems, try 'doas mount')\n");
        return 1;
    }

    char devpath[128];
    if (fstype && (strcmp(fstype, "tmpfs") == 0 || strcmp(fstype, "procfs") == 0 || strcmp(fstype, "proc") == 0 || strcmp(fstype, "sysfs") == 0 || strcmp(fstype, "sys") == 0)) {
        strncpy(devpath, dev, sizeof(devpath) - 1);
        devpath[sizeof(devpath) - 1] = '\0';
    } else if (strncmp(dev, "/dev/", 5) == 0) {
        strncpy(devpath, dev, sizeof(devpath) - 1);
        devpath[sizeof(devpath) - 1] = '\0';
    } else {
        snprintf(devpath, sizeof(devpath), "/dev/%s", dev);
    }

    if (syscall5(SYS_MOUNT, (uint64_t)devpath, (uint64_t)target, (uint64_t)(fstype ? fstype : ""), 0, 0) != 0) {
        fprintf(stderr, "mount: failed to mount %s on %s\n", devpath, target);
        return 1;
    }

    return 0;
}
