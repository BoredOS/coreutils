// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "auth_subr.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("Usage: %s [OPTION]\n", argv[0]);
            printf("Print the user name associated with the current effective user ID.\n");
            printf("Same as id -un.\n\n");
            printf("  -h, --help     display this help and exit\n");
            printf("      --version  output version information and exit\n");
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            printf("whoami (BoredOS coreutils) 1.0\n");
            return 0;
        } else {
            fprintf(stderr, "whoami: extra operand '%s'\n", argv[i]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return 1;
        }
    }

    uid_t uid = geteuid();
    passwd_entry_t pw;
    if (auth_get_passwd_by_uid(uid, &pw) && pw.name[0]) {
        puts(pw.name);
        return 0;
    }

    fprintf(stderr, "whoami: cannot find name for user ID %u\n", (unsigned int)uid);
    return 1;
}
