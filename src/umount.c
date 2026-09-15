// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>

static void resolve_target(const char *arg, char *out, size_t out_len) {
    strncpy(out, arg, out_len - 1);
    out[out_len - 1] = '\0';

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return;

    char line[256];
    const char *bare_arg = arg;
    if (strncmp(bare_arg, "/dev/", 5) == 0) {
        bare_arg += 5;
    }

    while (fgets(line, sizeof(line), f)) {
        char dev[128] = {0};
        char mnt[128] = {0};
        char type[64] = {0};
        if (sscanf(line, "%127s %127s %63s", dev, mnt, type) >= 2) {
            const char *bare_dev = dev;
            if (strncmp(bare_dev, "/dev/", 5) == 0) {
                bare_dev += 5;
            }
            if (strcmp(arg, dev) == 0 || strcmp(bare_arg, bare_dev) == 0) {
                strncpy(out, mnt, out_len - 1);
                out[out_len - 1] = '\0';
                break;
            }
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <mountpoint|device> ...\n", argv[0]);
        return 1;
    }

    if (getuid() != 0 && geteuid() != 0) {
        fprintf(stderr, "umount: Permission denied (only root can unmount filesystems, try 'doas umount')\n");
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        char target[256];
        resolve_target(argv[i], target, sizeof(target));
        if (sys_disk_umount(target) != 0 && sys_disk_umount(argv[i]) != 0) {
            fprintf(stderr, "umount: %s: unmount failed (resource busy or invalid path)\n", argv[i]);
            status = 1;
        }
    }

    return status;
}
