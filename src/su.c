// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "auth_subr.h"
#include "libcrypt_sha512.h"

int main(int argc, char **argv) {
    bool login_shell = false;
    const char *target_user = "root";
    const char *command = NULL;

    int idx = 1;
    while (idx < argc) {
        if (strcmp(argv[idx], "-") == 0 || strcmp(argv[idx], "-l") == 0 || strcmp(argv[idx], "--login") == 0) {
            login_shell = true;
            idx++;
        } else if (strcmp(argv[idx], "-c") == 0 && idx + 1 < argc) {
            command = argv[idx + 1];
            idx += 2;
        } else if (argv[idx][0] != '-') {
            target_user = argv[idx];
            idx++;
            break;
        } else {
            fprintf(stderr, "Usage: su [-] [username] [-c command]\n");
            return 1;
        }
    }

    if (idx < argc && !command) {
        if (strcmp(argv[idx], "-c") == 0 && idx + 1 < argc) {
            command = argv[idx + 1];
        }
    }

    passwd_entry_t pw;
    if (!auth_get_passwd_by_name(target_user, &pw)) {
        fprintf(stderr, "su: user '%s' does not exist\n", target_user);
        return 1;
    }

    uid_t caller_uid = getuid();

    if (caller_uid != 0) {
        shadow_entry_t sh;
        bool has_shadow = auth_get_shadow(target_user, &sh);
        if (!has_shadow || sh.hash[0] == '!' || sh.hash[0] == '*') {
            fprintf(stderr, "su: Authentication failure\n");
            return 1;
        }

        char password[128];
        if (!auth_read_password("Password: ", password, sizeof(password))) {
            return 1;
        }

        bool ok = false;
        if (sh.hash[0] == '\0') {
            ok = (password[0] == '\0');
        } else {
            ok = sha512_crypt_verify(password, sh.hash);
        }
        memset(password, 0, sizeof(password));

        if (!ok) {
            fprintf(stderr, "su: Authentication failure\n");
            return 1;
        }
    }

    gid_t groups[32];
    int ngroups = 0;
    groups[ngroups++] = pw.gid;

    FILE *gf = fopen(PATH_GROUP, "r");
    if (gf) {
        char gline[512];
        while (fgets(gline, sizeof(gline), gf)) {
            char *nl = strchr(gline, '\n');
            if (nl) *nl = '\0';
            if (gline[0] == '#' || gline[0] == '\0') continue;

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

    extern int setgroups(size_t size, const gid_t *list);
    setgroups(ngroups, groups);

    extern int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
    extern int setresuid(uid_t ruid, uid_t euid, uid_t suid);

    if (setresgid(pw.gid, pw.gid, pw.gid) != 0 ||
        setresuid(pw.uid, pw.uid, pw.uid) != 0) {
        fprintf(stderr, "su: failed to drop privileges\n");
        return 1;
    }

    if (getuid() != pw.uid || geteuid() != pw.uid ||
        getgid() != pw.gid || getegid() != pw.gid) {
        fprintf(stderr, "su: privilege drop verification failed\n");
        return 1;
    }

    auth_close_fds_ge_3();

    const char *shell_path = pw.shell[0] ? pw.shell : "/bin/bsh";

    char cur_dir[256] = "";
    getcwd(cur_dir, sizeof(cur_dir));

    if (login_shell || strcmp(cur_dir, "/") == 0) {
        if (chdir(pw.dir) != 0) {
            if (chdir("/") != 0) {}
        }
    }

    if (login_shell) {
        clearenv();
        setenv("PATH", (pw.uid == 0) ? "/bin:/usr/bin:/sbin:/usr/sbin" : "/bin:/usr/bin:/usr/local/bin", 1);
        setenv("TERM", "ansi", 1);
    }

    setenv("HOME", pw.dir[0] ? pw.dir : "/", 1);
    setenv("USER", pw.name, 1);
    setenv("LOGNAME", pw.name, 1);
    setenv("SHELL", shell_path, 1);

    const char *base_name = strrchr(shell_path, '/');
    base_name = base_name ? (base_name + 1) : shell_path;

    char argv0[64];
    if (login_shell) {
        snprintf(argv0, sizeof(argv0), "-%s", base_name);
    } else {
        snprintf(argv0, sizeof(argv0), "%s", base_name);
    }

    if (command) {
        char *const cmd_args[] = { argv0, "-c", (char *)command, NULL };
        execv(shell_path, cmd_args);
    } else {
        char *const def_args[] = { argv0, NULL };
        execv(shell_path, def_args);
    }

    perror("su: exec shell");
    return 1;
}
