// Copyright (c) 2026 zeyadhost (https://github.com/zeyadhost)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <syscall.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [FILE]...\n", prog);
    fprintf(stderr, "Synchronize cached writes to persistent storage\n");
}

int main(int argc, char **argv) {
    int ret = 0;
    int files_synced = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') continue;
            files_synced++;
            int fd = open(argv[i], O_RDONLY);
            if (fd >= 0) {
                if (syscall1(SYS_SYNCFS, (uint64_t)fd) != 0) {
                    fprintf(stderr, "sync: error syncing '%s': %s\n", argv[i], strerror(errno));
                    ret = 1;
                }
                close(fd);
            } else {
                fprintf(stderr, "sync: error opening '%s': %s\n", argv[i], strerror(errno));
                ret = 1;
            }
        }
        if (files_synced == 0) {
            syscall0(SYS_SYNC);
        }
    } else {
        syscall0(SYS_SYNC);
    }
    return ret;
}
