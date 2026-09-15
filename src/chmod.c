// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

static bool parse_mode_string(const char *str, mode_t current_mode, mode_t *new_mode) {
    char *endptr = NULL;
    long val = strtol(str, &endptr, 8);
    if (endptr && *endptr == '\0' && val >= 0 && val <= 07777) {
        *new_mode = (mode_t)val;
        return true;
    }

    mode_t cur_umask = umask(0);
    umask(cur_umask);

    mode_t res = current_mode;
    const char *p = str;
    while (*p) {
        mode_t who = 0;
        bool explicit_who = false;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            if (*p == 'u') who |= 04700;
            if (*p == 'g') who |= 02070;
            if (*p == 'o') who |= 01007;
            if (*p == 'a') who |= 07777;
            p++;
            explicit_who = true;
        }
        if (!explicit_who) who = 07777;

        char op = *p++;
        if (op != '+' && op != '-' && op != '=') return false;

        mode_t perm = 0;
        while (*p == 'r' || *p == 'w' || *p == 'x' || *p == 's' || *p == 't') {
            if (*p == 'r') {
                if (who & 0700) perm |= 0400;
                if (who & 0070) perm |= 0040;
                if (who & 0007) perm |= 0004;
            }
            if (*p == 'w') {
                if (who & 0700) perm |= 0200;
                if (who & 0070) perm |= 0020;
                if (who & 0007) perm |= 0002;
            }
            if (*p == 'x') {
                if (who & 0700) perm |= 0100;
                if (who & 0070) perm |= 0010;
                if (who & 0007) perm |= 0001;
            }
            if (*p == 's') {
                if (who & 0700) perm |= 04000;
                if (who & 0070) perm |= 02000;
            }
            if (*p == 't') {
                perm |= 01000;
            }
            p++;
        }

        if (!explicit_who && (op == '+' || op == '=')) {
            perm &= ~cur_umask;
        }

        if (op == '+') res |= perm;
        else if (op == '-') res &= ~perm;
        else if (op == '=') {
            res &= ~who;
            res |= perm;
        }

        if (*p == ',') p++;
        else if (*p != '\0') return false;
    }

    *new_mode = res & 07777;
    return true;
}

static int do_chmod(const char *path, const char *mode_str, bool recursive) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        perror(path);
        return 1;
    }

    if (S_ISLNK(st.st_mode)) {
        if (!recursive) {
            if (stat(path, &st) != 0) {
                perror(path);
                return 1;
            }
        } else {
            return 0;
        }
    }

    mode_t new_mode = 0;
    if (!parse_mode_string(mode_str, st.st_mode, &new_mode)) {
        fprintf(stderr, "chmod: invalid mode '%s'\n", mode_str);
        return 1;
    }

    int ret = 0;
    if (chmod(path, new_mode) != 0) {
        perror(path);
        ret = 1;
    }

    if (recursive && S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                char sub_path[1024];
                snprintf(sub_path, sizeof(sub_path), "%s/%s", path, de->d_name);
                if (do_chmod(sub_path, mode_str, recursive) != 0) {
                    ret = 1;
                }
            }
            closedir(dir);
        }
    }

    return ret;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: chmod [-R] <mode> <file...>\n");
        return 1;
    }

    bool recursive = false;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0' && !parse_mode_string(argv[arg_idx], 0, &(mode_t){0})) {
        if (strcmp(argv[arg_idx], "-R") == 0 || strcmp(argv[arg_idx], "--recursive") == 0) {
            recursive = true;
        } else if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        } else {
            fprintf(stderr, "chmod: unrecognized option '%s'\n", argv[arg_idx]);
            return 1;
        }
        arg_idx++;
    }

    if (arg_idx >= argc) {
        fprintf(stderr, "Usage: chmod [-R] <mode> <file...>\n");
        return 1;
    }

    const char *mode_str = argv[arg_idx++];
    if (arg_idx >= argc) {
        fprintf(stderr, "Usage: chmod [-R] <mode> <file...>\n");
        return 1;
    }

    int status = 0;
    for (int i = arg_idx; i < argc; i++) {
        if (do_chmod(argv[i], mode_str, recursive) != 0) {
            status = 1;
        }
    }

    return status;
}
