// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int cat_fd(FILE *f, const char *name) {
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (fwrite(buf, 1, n, stdout) != n) {
            fprintf(stderr, "cat: write error\n");
            return 1;
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return cat_fd(stdin, "<stdin>");
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (cat_fd(stdin, "<stdin>") != 0) ret = 1;
            continue;
        }

        FILE *f = fopen(argv[i], "r");
        if (!f) {
            if (errno == EACCES || errno == EPERM) {
                fprintf(stderr, "cat: %s: Permission denied\n", argv[i]);
            } else if (errno == ENOENT) {
                fprintf(stderr, "cat: %s: No such file or directory\n", argv[i]);
            } else if (errno == EISDIR) {
                fprintf(stderr, "cat: %s: Is a directory\n", argv[i]);
            } else {
                fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
            }
            ret = 1;
            continue;
        }

        if (cat_fd(f, argv[i]) != 0) ret = 1;
        fclose(f);
    }
    return ret;
}
