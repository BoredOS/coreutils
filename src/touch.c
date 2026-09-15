// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

int main(int argc, char **argv) {
    bool no_create = false;
    int opt_idx = 1;

    while (opt_idx < argc && argv[opt_idx][0] == '-' && argv[opt_idx][1] != '\0') {
        if (strcmp(argv[opt_idx], "--") == 0) {
            opt_idx++;
            break;
        }
        if (strcmp(argv[opt_idx], "-c") == 0 || strcmp(argv[opt_idx], "--no-create") == 0) {
            no_create = true;
        }
        opt_idx++;
    }

    if (opt_idx >= argc) {
        fprintf(stderr, "Usage: touch [-c] <file>...\n");
        return 1;
    }

    int ret = 0;
    for (int i = opt_idx; i < argc; i++) {
        const char *path = argv[i];
        int flags = O_WRONLY | O_NONBLOCK;
        if (!no_create) {
            flags |= O_CREAT;
        }

        int fd = open(path, flags, 0666);
        if (fd < 0) {
            if (no_create && errno == ENOENT) {
                // Not an error with -c if file does not exist
                continue;
            }
            if (errno == EACCES || errno == EPERM) {
                fprintf(stderr, "touch: %s: Permission denied\n", path);
            } else if (errno == ENOENT) {
                fprintf(stderr, "touch: %s: No such file or directory\n", path);
            } else if (errno == EISDIR) {
                fprintf(stderr, "touch: %s: Is a directory\n", path);
            } else {
                fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
            }
            ret = 1;
            continue;
        }
        close(fd);
    }

    return ret;
}
