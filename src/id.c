// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "auth_subr.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] [username]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -u, --user     Print only user ID\n");
    fprintf(stderr, "  -g, --group    Print only group ID\n");
    fprintf(stderr, "  -G, --groups   Print all group IDs\n");
    fprintf(stderr, "  -n, --name     Print name instead of number (with -u, -g, -G)\n");
    fprintf(stderr, "  -r, --real     Print real ID instead of effective\n");
    fprintf(stderr, "  -h, --help     Display this help message\n");
}

static const char *get_user_name(uid_t uid, char *buf, size_t size) {
    passwd_entry_t pw;
    if (auth_get_passwd_by_uid(uid, &pw)) {
        strncpy(buf, pw.name, size - 1);
        buf[size - 1] = '\0';
        return buf;
    }
    snprintf(buf, size, "%u", (unsigned int)uid);
    return buf;
}

static const char *get_group_name(gid_t gid, char *buf, size_t size) {
    group_entry_t grp;
    if (auth_get_group_by_gid(gid, &grp)) {
        strncpy(buf, grp.name, size - 1);
        buf[size - 1] = '\0';
        return buf;
    }
    snprintf(buf, size, "%u", (unsigned int)gid);
    return buf;
}

int main(int argc, char **argv) {
    int print_u = 0;
    int print_g = 0;
    int print_G = 0;
    int print_n = 0;
    int real_flag = 0;
    const char *target_user = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--user")) {
            print_u = 1;
        } else if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--group")) {
            print_g = 1;
        } else if (!strcmp(argv[i], "-G") || !strcmp(argv[i], "--groups")) {
            print_G = 1;
        } else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--name")) {
            print_n = 1;
        } else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--real")) {
            real_flag = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            target_user = argv[i];
        } else {
            fprintf(stderr, "id: extra operand '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    uid_t ruid, euid;
    gid_t rgid, egid;
    gid_t groups[32];
    int ngroups = 0;

    if (target_user) {
        passwd_entry_t pw;
        if (!auth_get_passwd_by_name(target_user, &pw)) {
            fprintf(stderr, "id: '%s': no such user\n", target_user);
            return 1;
        }
        ruid = euid = pw.uid;
        rgid = egid = pw.gid;
        groups[ngroups++] = pw.gid;

        FILE *gf = fopen(PATH_GROUP, "r");
        if (gf) {
            char gline[512];
            while (fgets(gline, sizeof(gline), gf)) {
                char line_copy[512];
                strncpy(line_copy, gline, sizeof(line_copy) - 1);
                char *gptr = line_copy;
                strsep(&gptr, ":");
                strsep(&gptr, ":");
                char *gid_str = strsep(&gptr, ":");
                char *members = strsep(&gptr, ":");
                if (gid_str && members) {
                    gid_t g = (gid_t)atol(gid_str);
                    char mem_copy[256];
                    strncpy(mem_copy, members, sizeof(mem_copy) - 1);
                    char *mptr = mem_copy;
                    char *m;
                    while ((m = strsep(&mptr, ",")) != NULL) {
                        while (*m == ' ') m++;
                        if (strcmp(m, pw.name) == 0) {
                            if (ngroups < 32) groups[ngroups++] = g;
                            break;
                        }
                    }
                }
            }
            fclose(gf);
        }
    } else {
        ruid = getuid();
        euid = geteuid();
        rgid = getgid();
        egid = getegid();
        ngroups = getgroups(32, groups);
    }

    char name_buf[64];

    if (print_u) {
        uid_t u = real_flag ? ruid : euid;
        if (print_n) printf("%s\n", get_user_name(u, name_buf, sizeof(name_buf)));
        else printf("%u\n", (unsigned int)u);
        return 0;
    }

    if (print_g) {
        gid_t g = real_flag ? rgid : egid;
        if (print_n) printf("%s\n", get_group_name(g, name_buf, sizeof(name_buf)));
        else printf("%u\n", (unsigned int)g);
        return 0;
    }

    if (print_G) {
        gid_t g = real_flag ? rgid : egid;
        if (print_n) printf("%s", get_group_name(g, name_buf, sizeof(name_buf)));
        else printf("%u", (unsigned int)g);

        for (int i = 0; i < ngroups; i++) {
            if (groups[i] != g) {
                if (print_n) printf(" %s", get_group_name(groups[i], name_buf, sizeof(name_buf)));
                else printf(" %u", (unsigned int)groups[i]);
            }
        }
        printf("\n");
        return 0;
    }

    char ubuf[64], gbuf[64];
    printf("uid=%u(%s) gid=%u(%s)",
           (unsigned int)ruid, get_user_name(ruid, ubuf, sizeof(ubuf)),
           (unsigned int)rgid, get_group_name(rgid, gbuf, sizeof(gbuf)));

    if (euid != ruid) {
        printf(" euid=%u(%s)", (unsigned int)euid, get_user_name(euid, ubuf, sizeof(ubuf)));
    }
    if (egid != rgid) {
        printf(" egid=%u(%s)", (unsigned int)egid, get_group_name(egid, gbuf, sizeof(gbuf)));
    }

    printf(" groups=%u(%s)", (unsigned int)rgid, get_group_name(rgid, gbuf, sizeof(gbuf)));
    if (egid != rgid) {
        printf(",%u(%s)", (unsigned int)egid, get_group_name(egid, gbuf, sizeof(gbuf)));
    }
    for (int i = 0; i < ngroups; i++) {
        if (groups[i] != rgid && groups[i] != egid) {
            printf(",%u(%s)", (unsigned int)groups[i], get_group_name(groups[i], gbuf, sizeof(gbuf)));
        }
    }
    printf("\n");

    return 0;
}
