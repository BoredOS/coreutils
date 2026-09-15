// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "auth_subr.h"
#include "libcrypt_sha512.h"

static void print_usage(void) {
    fprintf(stderr, "Usage: useradd [-u uid] [-g gid] [-G groups] [-d homedir] [-s shell] [-c comment] [-m] username\n");
}

int main(int argc, char **argv) {
    if (getuid() != 0 && geteuid() != 0) {
        fprintf(stderr, "useradd: Only root may add users to the system.\n");
        return 1;
    }

    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;
    char groups_arg[256] = {0};
    char homedir[128] = {0};
    char shell[64] = "/bin/bsh";
    char comment[64] = {0};
    bool create_home = false;
    char username[64] = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            uid = (uid_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            gid = (gid_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "-G") == 0 && i + 1 < argc) {
            strncpy(groups_arg, argv[++i], sizeof(groups_arg) - 1);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            strncpy(homedir, argv[++i], sizeof(homedir) - 1);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(shell, argv[++i], sizeof(shell) - 1);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            strncpy(comment, argv[++i], sizeof(comment) - 1);
        } else if (strcmp(argv[i], "-m") == 0) {
            create_home = true;
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

    passwd_entry_t existing;
    if (auth_get_passwd_by_name(username, &existing)) {
        fprintf(stderr, "useradd: user '%s' already exists\n", username);
        return 1;
    }

    if (uid == (uid_t)-1) {
        uid = auth_get_next_uid(1000);
    }
    if (gid == (gid_t)-1) {
        gid = (gid_t)uid;
    }
    if (!homedir[0]) {
        snprintf(homedir, sizeof(homedir), "/home/%s", username);
    }
    if (!comment[0]) {
        strncpy(comment, username, sizeof(comment) - 1);
    }

    passwd_entry_t pw;
    memset(&pw, 0, sizeof(pw));
    strncpy(pw.name, username, sizeof(pw.name) - 1);
    pw.uid = uid;
    pw.gid = gid;
    strncpy(pw.gecos, comment, sizeof(pw.gecos) - 1);
    strncpy(pw.dir, homedir, sizeof(pw.dir) - 1);
    strncpy(pw.shell, shell, sizeof(pw.shell) - 1);

    char password[128] = {0};
    if (isatty(0)) {
        char pass2[128] = {0};
        while (1) {
            if (!auth_read_password("Enter password: ", password, sizeof(password))) {
                password[0] = '\0';
                break;
            }
            if (password[0] == '\0') break;
            if (!auth_read_password("Retype password: ", pass2, sizeof(pass2))) {
                password[0] = '\0';
                break;
            }
            if (strcmp(password, pass2) == 0) break;
            printf("Passwords do not match. Try again.\n");
        }
        memset(pass2, 0, sizeof(pass2));
    }

    if (!auth_add_user(&pw, password)) {
        fprintf(stderr, "useradd: failed to add user\n");
        memset(password, 0, sizeof(password));
        return 1;
    }
    memset(password, 0, sizeof(password));

    if (create_home) {
        mkdir("/home", 0755);
        if (mkdir(homedir, 0750) == 0) {
            chown(homedir, uid, gid);
            chmod(homedir, 0750);
            auth_populate_skel(homedir, uid, gid);
        } else {
            perror("useradd: mkdir home");
        }
    }

    if (groups_arg[0]) {
        char *grp = strtok(groups_arg, ",");
        while (grp) {
            while (*grp == ' ') grp++;
            auth_add_user_to_group(username, grp);
            grp = strtok(NULL, ",");
        }
    }

    printf("useradd: user '%s' (uid %u, gid %u) created successfully\n",
           username, (unsigned int)uid, (unsigned int)gid);
    return 0;
}
