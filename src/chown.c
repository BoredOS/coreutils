// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

static bool parse_user_group(const char *spec, uid_t *uid, gid_t *gid) {
    char buf[256];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *sep = strchr(buf, ':');
    if (!sep) sep = strchr(buf, '.');

    char *user_part = buf;
    char *group_part = NULL;
    if (sep) {
        *sep = '\0';
        group_part = sep + 1;
    }

    *uid = (uid_t)-1;
    *gid = (gid_t)-1;

    if (user_part[0] != '\0') {
        struct passwd *pw = getpwnam(user_part);
        if (pw) {
            *uid = pw->pw_uid;
        } else {
            char *end = NULL;
            long val = strtol(user_part, &end, 10);
            if (end && *end == '\0') {
                *uid = (uid_t)val;
            } else {
                fprintf(stderr, "chown: invalid user '%s'\n", user_part);
                return false;
            }
        }
    }

    if (group_part && group_part[0] != '\0') {
        struct group *gr = getgrnam(group_part);
        if (gr) {
            *gid = gr->gr_gid;
        } else {
            char *end = NULL;
            long val = strtol(group_part, &end, 10);
            if (end && *end == '\0') {
                *gid = (gid_t)val;
            } else {
                fprintf(stderr, "chown: invalid group '%s'\n", group_part);
                return false;
            }
        }
    }

    return true;
}

static int do_chown(const char *path, uid_t uid, gid_t gid, bool recursive) {
    int ret = 0;
    if (lchown(path, uid, gid) != 0) {
        perror(path);
        ret = 1;
    }

    if (recursive) {
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            DIR *dir = opendir(path);
            if (dir) {
                struct dirent *de;
                while ((de = readdir(dir)) != NULL) {
                    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                    char sub_path[1024];
                    snprintf(sub_path, sizeof(sub_path), "%s/%s", path, de->d_name);
                    if (do_chown(sub_path, uid, gid, recursive) != 0) {
                        ret = 1;
                    }
                }
                closedir(dir);
            }
        }
    }

    return ret;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: chown [-R] <owner>[:<group>] <file...>\n");
        return 1;
    }

    bool recursive = false;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "-R") == 0 || strcmp(argv[arg_idx], "--recursive") == 0) {
            recursive = true;
        } else if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        } else {
            fprintf(stderr, "chown: unrecognized option '%s'\n", argv[arg_idx]);
            return 1;
        }
        arg_idx++;
    }

    if (arg_idx >= argc) {
        fprintf(stderr, "Usage: chown [-R] <owner>[:<group>] <file...>\n");
        return 1;
    }

    const char *spec = argv[arg_idx++];
    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;
    if (!parse_user_group(spec, &uid, &gid)) {
        return 1;
    }

    if (arg_idx >= argc) {
        fprintf(stderr, "Usage: chown [-R] <owner>[:<group>] <file...>\n");
        return 1;
    }

    int status = 0;
    for (int i = arg_idx; i < argc; i++) {
        if (do_chown(argv[i], uid, gid, recursive) != 0) {
            status = 1;
        }
    }

    return status;
}
