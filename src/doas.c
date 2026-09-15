// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "auth_subr.h"
#include "libcrypt_sha512.h"

#define DOAS_CONF_PATH "/etc/doas.conf"

typedef struct {
    bool permit;
    bool nopass;
    bool keepenv;
    char identity[64];  // "username" or ":groupname"
    char target[64];    // target user or empty for any
    char cmd[128];      // specific command or empty for any
} doas_rule_t;

static bool check_doas_conf_security(void) {
    struct stat st;
    if (stat(DOAS_CONF_PATH, &st) != 0) {
        fprintf(stderr, "doas: %s does not exist\n", DOAS_CONF_PATH);
        return false;
    }

    if (st.st_uid != 0) {
        fprintf(stderr, "doas: %s is not owned by root\n", DOAS_CONF_PATH);
        return false;
    }

    mode_t perm = st.st_mode & 0777;
    if (perm != 0400 && perm != 0600) {
        fprintf(stderr, "doas: %s has invalid permissions 0%o (must be mode 0400 or 0600)\n", DOAS_CONF_PATH, (unsigned int)perm);
        return false;
    }

    return true;
}

static bool match_rule(const doas_rule_t *r, const char *caller_user, const char *target_user, const char *cmd) {
    if (r->identity[0] == ':') {
        const char *group = r->identity + 1;
        if (!auth_user_in_group(caller_user, group)) return false;
    } else {
        if (strcmp(r->identity, caller_user) != 0) return false;
    }

    if (r->target[0] && strcmp(r->target, target_user) != 0) return false;

    if (r->cmd[0]) {
        const char *cmd_base = strrchr(cmd, '/');
        cmd_base = cmd_base ? (cmd_base + 1) : cmd;
        const char *rule_base = strrchr(r->cmd, '/');
        rule_base = rule_base ? (rule_base + 1) : r->cmd;
        if (strcmp(r->cmd, cmd) != 0 && strcmp(rule_base, cmd_base) != 0) return false;
    }

    return true;
}

int main(int argc, char **argv) {
    const char *target_user = "root";
    bool run_shell = false;
    bool non_interactive = false;

    int cmd_idx = 1;
    while (cmd_idx < argc && argv[cmd_idx][0] == '-') {
        if (strcmp(argv[cmd_idx], "-u") == 0 && cmd_idx + 1 < argc) {
            target_user = argv[++cmd_idx];
            cmd_idx++;
        } else if (strcmp(argv[cmd_idx], "-s") == 0) {
            run_shell = true;
            cmd_idx++;
        } else if (strcmp(argv[cmd_idx], "-n") == 0) {
            non_interactive = true;
            cmd_idx++;
        } else if (strcmp(argv[cmd_idx], "--") == 0) {
            cmd_idx++;
            break;
        } else {
            fprintf(stderr, "Usage: doas [-u user] [-s] [-n] command [args...]\n");
            return 1;
        }
    }

    if (cmd_idx >= argc && !run_shell) {
        run_shell = true;
    }

    uid_t caller_uid = getuid();
    passwd_entry_t caller_pw;
    if (!auth_get_passwd_by_uid(caller_uid, &caller_pw)) {
        fprintf(stderr, "doas: unknown caller uid %u\n", (unsigned int)caller_uid);
        return 1;
    }

    passwd_entry_t target_pw;
    if (!auth_get_passwd_by_name(target_user, &target_pw)) {
        fprintf(stderr, "doas: target user '%s' does not exist\n", target_user);
        return 1;
    }

    if (!check_doas_conf_security()) {
        return 1;
    }

    const char *cmd = run_shell ? (target_pw.shell[0] ? target_pw.shell : "/bin/bsh") : argv[cmd_idx];

    FILE *f = fopen(DOAS_CONF_PATH, "r");
    if (!f) {
        fprintf(stderr, "doas: cannot open %s\n", DOAS_CONF_PATH);
        return 1;
    }

    bool has_rule = false;
    doas_rule_t active_rule;
    memset(&active_rule, 0, sizeof(active_rule));

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *token = strtok(line, " \t");
        if (!token) continue;

        doas_rule_t r;
        memset(&r, 0, sizeof(r));

        if (strcmp(token, "permit") == 0) {
            r.permit = true;
        } else if (strcmp(token, "deny") == 0) {
            r.permit = false;
        } else {
            continue;
        }

        while ((token = strtok(NULL, " \t")) != NULL) {
            if (strcmp(token, "nopass") == 0) {
                r.nopass = true;
            } else if (strcmp(token, "keepenv") == 0) {
                r.keepenv = true;
            } else if (strcmp(token, "as") == 0) {
                char *tgt = strtok(NULL, " \t");
                if (tgt) strncpy(r.target, tgt, sizeof(r.target) - 1);
            } else if (strcmp(token, "cmd") == 0) {
                char *c = strtok(NULL, " \t");
                if (c) strncpy(r.cmd, c, sizeof(r.cmd) - 1);
            } else if (!r.identity[0]) {
                strncpy(r.identity, token, sizeof(r.identity) - 1);
            }
        }

        if (match_rule(&r, caller_pw.name, target_user, cmd)) {
            active_rule = r;
            has_rule = true;
        }
    }
    fclose(f);

    if (!has_rule || !active_rule.permit) {
        fprintf(stderr, "doas: Operation not permitted\n");
        return 1;
    }

    if (!active_rule.nopass && caller_uid != 0) {
        if (non_interactive) {
            fprintf(stderr, "doas: Password required\n");
            return 1;
        }

        shadow_entry_t caller_sh;
        if (!auth_get_shadow(caller_pw.name, &caller_sh) ||
            caller_sh.hash[0] == '!' || caller_sh.hash[0] == '*') {
            fprintf(stderr, "doas: Authentication failed\n");
            return 1;
        }

        char prompt[128];
        snprintf(prompt, sizeof(prompt), "doas (%s@boredos) password: ", caller_pw.name);

        char password[128];
        if (!auth_read_password(prompt, password, sizeof(password))) {
            return 1;
        }

        bool ok = false;
        if (caller_sh.hash[0] == '\0') {
            ok = (password[0] == '\0');
        } else {
            ok = sha512_crypt_verify(password, caller_sh.hash);
        }
        memset(password, 0, sizeof(password));

        if (!ok) {
            fprintf(stderr, "doas: Authentication failed\n");
            return 1;
        }
    }

    gid_t groups[32];
    int ngroups = 0;
    groups[ngroups++] = target_pw.gid;

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
                    if (strcmp(m, target_pw.name) == 0) {
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

    if (setresgid(target_pw.gid, target_pw.gid, target_pw.gid) != 0 ||
        setresuid(target_pw.uid, target_pw.uid, target_pw.uid) != 0) {
        fprintf(stderr, "doas: failed to drop privileges\n");
        return 1;
    }

    if (getuid() != target_pw.uid || geteuid() != target_pw.uid ||
        getgid() != target_pw.gid || getegid() != target_pw.gid) {
        fprintf(stderr, "doas: privilege drop verification failed\n");
        return 1;
    }

    auth_close_fds_ge_3();

    if (!active_rule.keepenv) {
        clearenv();
        setenv("PATH", (target_pw.uid == 0) ? "/bin:/usr/bin:/sbin:/usr/sbin" : "/bin:/usr/bin:/usr/local/bin", 1);
        setenv("USER", target_pw.name, 1);
        setenv("LOGNAME", target_pw.name, 1);
        setenv("HOME", target_pw.dir[0] ? target_pw.dir : "/", 1);
        setenv("SHELL", target_pw.shell[0] ? target_pw.shell : "/bin/bsh", 1);
        setenv("DOAS_USER", caller_pw.name, 1);
        setenv("TERM", "ansi", 1);
    } else {
        // ALWAYS strip unsafe linker & shell variables even with keepenv!
        unsetenv("LD_PRELOAD");
        unsetenv("LD_LIBRARY_PATH");
        unsetenv("IFS");
        setenv("DOAS_USER", caller_pw.name, 1);
    }

    const char *target_shell = target_pw.shell[0] ? target_pw.shell : "/bin/bsh";

    if (run_shell) {
        char *const sh_args[] = { (char *)target_shell, "-l", NULL };
        execv(target_shell, sh_args);
    } else {
        execvp(cmd, &argv[cmd_idx]);
        if (strstr(cmd, ".elf") == NULL) {
            char elf_cmd[256];
            snprintf(elf_cmd, sizeof(elf_cmd), "%s.elf", cmd);
            argv[cmd_idx] = elf_cmd;
            execvp(elf_cmd, &argv[cmd_idx]);
            argv[cmd_idx] = (char *)cmd;
        }

        FILE *sf = fopen(cmd, "r");
        if (sf) {
            char line[256];
            if (fgets(line, sizeof(line), sf)) {
                fclose(sf);
                char *nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                nl = strchr(line, '\r');
                if (nl) *nl = '\0';

                char *interp = NULL;
                char *interp_arg = NULL;
                if (line[0] == '#' && line[1] == '!') {
                    char *p = line + 2;
                    while (*p == ' ' || *p == '\t') p++;
                    interp = p;
                    while (*p && *p != ' ' && *p != '\t') p++;
                    if (*p) {
                        *p++ = '\0';
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p) interp_arg = p;
                    }
                }

                if (!interp || !interp[0]) {
                    interp = (char *)target_shell;
                }

                int max_args = argc + 4;
                char **script_argv = malloc(sizeof(char *) * max_args);
                if (script_argv) {
                    int sidx = 0;
                    script_argv[sidx++] = interp;
                    if (interp_arg && interp_arg[0]) {
                        script_argv[sidx++] = interp_arg;
                    }
                    script_argv[sidx++] = (char *)cmd;
                    for (int i = cmd_idx + 1; i < argc; i++) {
                        script_argv[sidx++] = argv[i];
                    }
                    script_argv[sidx] = NULL;
                    execvp(interp, script_argv);
                }
            } else {
                fclose(sf);
            }
        }

        size_t full_cmd_len = 0;
        for (int i = cmd_idx; i < argc; i++) {
            full_cmd_len += strlen(argv[i]) + 3;
        }
        char *shell_cmd = malloc(full_cmd_len + 1);
        if (shell_cmd) {
            shell_cmd[0] = '\0';
            for (int i = cmd_idx; i < argc; i++) {
                if (i > cmd_idx) strcat(shell_cmd, " ");
                if (strchr(argv[i], ' ') || strchr(argv[i], '\t')) {
                    strcat(shell_cmd, "\"");
                    strcat(shell_cmd, argv[i]);
                    strcat(shell_cmd, "\"");
                } else {
                    strcat(shell_cmd, argv[i]);
                }
            }
            char *fallback_argv[] = { (char *)target_shell, "-c", shell_cmd, NULL };
            execvp(target_shell, fallback_argv);
            if (strstr(target_shell, ".elf") == NULL) {
                char elf_shell[256];
                snprintf(elf_shell, sizeof(elf_shell), "%s.elf", target_shell);
                fallback_argv[0] = elf_shell;
                execvp(elf_shell, fallback_argv);
            }
            free(shell_cmd);
        }
    }

    perror("doas: exec");
    return 1;
}
