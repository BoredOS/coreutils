// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "auth_subr.h"
#include "libcrypt_sha512.h"

static const char *DUMMY_HASH = "$6$16randomsaltval$w5p6s0K771G3q8X6O.6x4.l2q5YV8K8wK4G/z5U1V.4G/y1W5P/z5U1V.4G/y1W5P6s0K771G3q8X6O.6x4.l2q5YV8K8wK4G";

int main(int argc, char **argv) {
    char username[64] = {0};
    bool pre_authenticated = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            if (getuid() == 0) {
                pre_authenticated = true;
                strncpy(username, argv[i + 1], sizeof(username) - 1);
            }
            i++;
        } else if (!username[0] && argv[i][0] != '-') {
            strncpy(username, argv[i], sizeof(username) - 1);
        }
    }

    int attempts = 0;
    while (attempts < 3) {
        if (!username[0]) {
            printf("boredos login: ");
            fflush(stdout);
            int len = auth_read_line(0, username, sizeof(username), 1 /* echo */);
            if (len <= 0) {
                return 1;
            }
            if (!username[0]) continue;
        }

        char *start = username;
        while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
        char *end = start + strlen(start);
        while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t' || *(end - 1) == '\r' || *(end - 1) == '\n')) {
            end--;
            *end = '\0';
        }
        if (start != username) {
            memmove(username, start, strlen(start) + 1);
        }
        if (!username[0]) continue;

        passwd_entry_t pw;
        bool user_exists = auth_get_passwd_by_name(username, &pw);

        shadow_entry_t sh;
        bool shadow_exists = false;
        if (user_exists) {
            shadow_exists = auth_get_shadow(username, &sh);
        }

        bool ok = false;
        if (pre_authenticated && user_exists) {
            ok = true;
        } else {
            char password[128];
            if (!auth_read_password("Password: ", password, sizeof(password))) {
                return 1;
            }

            if (user_exists && shadow_exists) {
                if (sh.hash[0] == '!' || sh.hash[0] == '*') {
                    ok = false;
                } else if (sh.hash[0] == '\0') {
                    ok = (password[0] == '\0');
                } else {
                    ok = sha512_crypt_verify(password, sh.hash);
                }
            } else {
                sha512_crypt_verify(password, DUMMY_HASH);
                ok = false;
            }

            memset(password, 0, sizeof(password));
        }

        if (ok) {
            auth_populate_skel(pw.dir, pw.uid, pw.gid);

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

            char *tty = ttyname(0);
            if (!tty) tty = ttyname(1);
            if (tty) {
                chown(tty, pw.uid, 5 /* tty group */);
                chmod(tty, 0620);
            }

            extern int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
            extern int setresuid(uid_t ruid, uid_t euid, uid_t suid);

            if (setresgid(pw.gid, pw.gid, pw.gid) != 0 ||
                setresuid(pw.uid, pw.uid, pw.uid) != 0) {
                fprintf(stderr, "login: failed to drop privileges\n");
                return 1;
            }

            if (getuid() != pw.uid || geteuid() != pw.uid ||
                getgid() != pw.gid || getegid() != pw.gid) {
                fprintf(stderr, "login: privilege drop verification failed\n");
                return 1;
            }

            auth_close_fds_ge_3();

            clearenv();
            setenv("HOME", pw.dir[0] ? pw.dir : "/", 1);
            setenv("USER", pw.name, 1);
            setenv("LOGNAME", pw.name, 1);
            setenv("SHELL", pw.shell[0] ? pw.shell : "/bin/bsh", 1);
            setenv("PATH", "/bin:/usr/bin:/usr/local/bin", 1);
            setenv("TERM", "ansi", 1);

            if (chdir(pw.dir) != 0) {
                if (chdir("/") != 0) {
                }
            }

            const char *shell_path = pw.shell[0] ? pw.shell : "/bin/bsh";
            const char *base_name = strrchr(shell_path, '/');
            base_name = base_name ? (base_name + 1) : shell_path;

            char login_argv0[64];
            snprintf(login_argv0, sizeof(login_argv0), "-%s", base_name);

            char *const shell_args[] = { login_argv0, NULL };

            execv(shell_path, shell_args);

            perror("exec shell");
            return 1;
        }

        sleep(2);
        printf("Login incorrect\n\n");
        fflush(stdout);
        username[0] = '\0';
        attempts++;
    }

    return 1;
}
