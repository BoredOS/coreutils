// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "auth_subr.h"

static void print_usage(void) {
    fprintf(stderr, "Usage: usermod [-u uid] [-g gid] [-G groups [-a]] [-s shell] [-d homedir] [-L] [-U] username\n");
}

int main(int argc, char **argv) {
    if (getuid() != 0 && geteuid() != 0) {
        fprintf(stderr, "usermod: Only root may modify users.\n");
        return 1;
    }

    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;
    char groups_arg[256] = {0};
    bool append_groups = false;
    char homedir[128] = {0};
    char shell[64] = {0};
    bool lock = false;
    bool unlock = false;
    char username[64] = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            uid = (uid_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            gid = (gid_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "-G") == 0 && i + 1 < argc) {
            strncpy(groups_arg, argv[++i], sizeof(groups_arg) - 1);
        } else if (strcmp(argv[i], "-a") == 0) {
            append_groups = true;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            strncpy(homedir, argv[++i], sizeof(homedir) - 1);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(shell, argv[++i], sizeof(shell) - 1);
        } else if (strcmp(argv[i], "-L") == 0) {
            lock = true;
        } else if (strcmp(argv[i], "-U") == 0) {
            unlock = true;
        } else if (argv[i][0] == '-') {
            print_usage();
            return 1;
        } else {
            strncpy(username, argv[i], sizeof(username) - 1);
        }
    }

    if (!username[0]) {
        print_usage();
        return 1;
    }

    passwd_entry_t pw;
    if (!auth_get_passwd_by_name(username, &pw)) {
        fprintf(stderr, "usermod: user '%s' does not exist\n", username);
        return 1;
    }

    if (uid != (uid_t)-1 || gid != (gid_t)-1 || shell[0] || homedir[0]) {
        if (!auth_modify_user(username, uid, gid, shell, homedir)) {
            fprintf(stderr, "usermod: failed to update /etc/passwd\n");
            return 1;
        }
    }

    if (lock) {
        shadow_entry_t sh;
        if (auth_get_shadow(username, &sh)) {
            if (sh.hash[0] != '!') {
                char locked_hash[132];
                snprintf(locked_hash, sizeof(locked_hash), "!%s", sh.hash);
                auth_update_shadow(username, locked_hash);
            }
        }
    } else if (unlock) {
        shadow_entry_t sh;
        if (auth_get_shadow(username, &sh)) {
            if (sh.hash[0] == '!') {
                if (sh.hash[1] == '\0') {
                    fprintf(stderr, "usermod: cannot unlock account '%s' with no password set\n", username);
                    return 1;
                }
                auth_update_shadow(username, sh.hash + 1);
            }
        }
    }

    if (groups_arg[0]) {
        if (!append_groups) {
            auth_remove_user_from_groups(username);
        }
        char *gptr = groups_arg;
        char *grp;
        while ((grp = strsep(&gptr, ",")) != NULL) {
            while (*grp == ' ') grp++;
            if (*grp) auth_add_user_to_group(username, grp);
        }
    }

    printf("usermod: user '%s' modified successfully\n", username);
    return 0;
}
