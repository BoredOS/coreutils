// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "auth_subr.h"

static void remove_dir_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        unlink(path);
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%s", path, de->d_name);
        struct stat st;
        if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_dir_recursive(sub);
        } else {
            unlink(sub);
        }
    }
    closedir(d);
    rmdir(path);
}

int main(int argc, char **argv) {
    if (getuid() != 0 && geteuid() != 0) {
        fprintf(stderr, "userdel: Only root may delete users.\n");
        return 1;
    }

    bool remove_home = false;
    char username[64] = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            remove_home = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Usage: userdel [-r] username\n");
            return 1;
        } else {
            strncpy(username, argv[i], sizeof(username) - 1);
        }
    }

    if (!username[0]) {
        fprintf(stderr, "Usage: userdel [-r] username\n");
        return 1;
    }

    passwd_entry_t pw;
    if (!auth_get_passwd_by_name(username, &pw)) {
        fprintf(stderr, "userdel: user '%s' does not exist\n", username);
        return 1;
    }

    if (remove_home && pw.dir[0] && strcmp(pw.dir, "/") != 0) {
        remove_dir_recursive(pw.dir);
    }

    if (!auth_delete_user(username)) {
        fprintf(stderr, "userdel: failed to delete user '%s'\n", username);
        return 1;
    }

    printf("userdel: user '%s' deleted successfully\n", username);
    return 0;
}
