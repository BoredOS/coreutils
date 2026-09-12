// Copyright (c) 2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -u, --user     Print only effective user ID\n");
    fprintf(stderr, "  -g, --group    Print only effective group ID\n");
    fprintf(stderr, "  -G, --groups   Print all group IDs\n");
    fprintf(stderr, "  -r, --real     Print real ID instead of effective (with -u or -g)\n");
    fprintf(stderr, "  -h, --help     Display this help message\n");
}

int main(int argc, char **argv) {
    int print_u = 0;
    int print_g = 0;
    int print_G = 0;
    int real_flag = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--user")) {
            print_u = 1;
        } else if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--group")) {
            print_g = 1;
        } else if (!strcmp(argv[i], "-G") || !strcmp(argv[i], "--groups")) {
            print_G = 1;
        } else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--real")) {
            real_flag = 1;
        } else if (!strcmp(argv[i], "-ru") || !strcmp(argv[i], "-ur")) {
            print_u = 1;
            real_flag = 1;
        } else if (!strcmp(argv[i], "-rg") || !strcmp(argv[i], "-gr")) {
            print_g = 1;
            real_flag = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "id: extra operand '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    uid_t ruid = getuid();
    uid_t euid = geteuid();
    gid_t rgid = getgid();
    gid_t egid = getegid();

    gid_t groups[32];
    int ngroups = getgroups(32, groups);

    if (print_u) {
        printf("%u\n", real_flag ? ruid : euid);
        return 0;
    }

    if (print_g) {
        printf("%u\n", real_flag ? rgid : egid);
        return 0;
    }

    if (print_G) {
        printf("%u", real_flag ? rgid : egid);
        for (int i = 0; i < ngroups; i++) {
            if (groups[i] != (real_flag ? rgid : egid)) {
                printf(" %u", groups[i]);
            }
        }
        printf("\n");
        return 0;
    }

    if (ruid == 0) {
        printf("uid=0(root) gid=%u", rgid);
        if (rgid == 0) printf("(root)");
    } else {
        printf("uid=%u gid=%u", ruid, rgid);
    }

    if (euid != ruid) {
        printf(" euid=%u", euid);
        if (euid == 0) printf("(root)");
    }
    if (egid != rgid) {
        printf(" egid=%u", egid);
        if (egid == 0) printf("(root)");
    }

    printf(" groups=%u", rgid);
    if (rgid == 0) printf("(root)");
    if (egid != rgid) {
        printf(",%u", egid);
        if (egid == 0) printf("(root)");
    }
    for (int i = 0; i < ngroups; i++) {
        if (groups[i] != rgid && groups[i] != egid) {
            printf(",%u", groups[i]);
            if (groups[i] == 0) printf("(root)");
        }
    }

    printf("\n");
    return 0;
}
