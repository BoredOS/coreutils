// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "auth_subr.h"
#include "libcrypt_sha512.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <poll.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif

int auth_lock_shadow(void) {
    int attempts = 0;
    while (attempts < 10) {
        int fd = open(PATH_SHADOW_LOCK, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) {
            char pid_buf[32];
            int len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", getpid());
            write(fd, pid_buf, len);
            return fd;
        }

        if (errno == EEXIST) {
            struct stat st;
            if (stat(PATH_SHADOW_LOCK, &st) == 0) {
                time_t now = time(NULL);
                if (now > st.st_mtime + 15) {
                    // Stale lock (> 15 seconds), break it
                    unlink(PATH_SHADOW_LOCK);
                    continue;
                }
            }
        }
        usleep(100000);
        attempts++;
    }
    return -1;
}

void auth_unlock_shadow(int lock_fd) {
    if (lock_fd >= 0) {
        close(lock_fd);
    }
    unlink(PATH_SHADOW_LOCK);
}

static bool auth_atomic_replace(const char *tmp_path, const char *dest_path) {
    if (rename(tmp_path, dest_path) == 0) {
        return true;
    }
    unlink(dest_path);
    if (rename(tmp_path, dest_path) == 0) {
        return true;
    }
    unlink(tmp_path);
    return false;
}

bool auth_get_shadow(const char *username, shadow_entry_t *entry) {
    if (!username || !entry) return false;
    memset(entry, 0, sizeof(shadow_entry_t));

    FILE *f = fopen(PATH_SHADOW, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *ptr = line;
        char *token = strsep(&ptr, ":");
        if (!token || strcmp(token, username) != 0) continue;

        strncpy(entry->name, token, sizeof(entry->name) - 1);

        char *hash = strsep(&ptr, ":");
        if (hash) strncpy(entry->hash, hash, sizeof(entry->hash) - 1);

        char *last = strsep(&ptr, ":");
        entry->lastchange = (last && *last) ? atol(last) : 0;

        char *min = strsep(&ptr, ":");
        entry->min = (min && *min) ? atol(min) : 0;

        char *max = strsep(&ptr, ":");
        entry->max = (max && *max) ? atol(max) : 99999;

        char *warn = strsep(&ptr, ":");
        entry->warn = (warn && *warn) ? atol(warn) : 7;

        char *inact = strsep(&ptr, ":");
        entry->inact = (inact && *inact) ? atol(inact) : -1;

        char *expire = strsep(&ptr, ":");
        entry->expire = (expire && *expire) ? atol(expire) : -1;

        found = true;
        break;
    }

    fclose(f);
    return found;
}

bool auth_update_shadow(const char *username, const char *new_hash) {
    if (!username || !new_hash) return false;

    int lock_fd = auth_lock_shadow();
    if (lock_fd < 0) return false;

    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", PATH_SHADOW, getpid());

    int out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out_fd < 0) {
        auth_unlock_shadow(lock_fd);
        return false;
    }
    FILE *out = fdopen(out_fd, "w");
    if (!out) {
        close(out_fd);
        unlink(tmp_path);
        auth_unlock_shadow(lock_fd);
        return false;
    }

    FILE *in = fopen(PATH_SHADOW, "r");
    bool replaced = false;
    long days_since_epoch = (long)(time(NULL) / 86400);

    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char line_copy[512];
            strncpy(line_copy, line, sizeof(line_copy) - 1);
            char *colon = strchr(line_copy, ':');
            if (colon) *colon = '\0';

            if (strcmp(line_copy, username) == 0) {
                fprintf(out, "%s:%s:%ld:0:99999:7:::\n", username, new_hash, days_since_epoch);
                replaced = true;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }

    if (!replaced) {
        fprintf(out, "%s:%s:%ld:0:99999:7:::\n", username, new_hash, days_since_epoch);
    }

    fflush(out);
    fclose(out);

    chmod(tmp_path, 0600);
    chown(tmp_path, 0, 0);

    bool ok = auth_atomic_replace(tmp_path, PATH_SHADOW);

    auth_unlock_shadow(lock_fd);
    return ok;
}

bool auth_delete_shadow(const char *username) {
    if (!username) return false;

    int lock_fd = auth_lock_shadow();
    if (lock_fd < 0) return false;

    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", PATH_SHADOW, getpid());

    int out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out_fd < 0) {
        auth_unlock_shadow(lock_fd);
        return false;
    }
    FILE *out = fdopen(out_fd, "w");
    if (!out) {
        close(out_fd);
        unlink(tmp_path);
        auth_unlock_shadow(lock_fd);
        return false;
    }

    FILE *in = fopen(PATH_SHADOW, "r");
    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char line_copy[512];
            strncpy(line_copy, line, sizeof(line_copy) - 1);
            char *colon = strchr(line_copy, ':');
            if (colon) *colon = '\0';

            if (strcmp(line_copy, username) != 0) {
                fputs(line, out);
            }
        }
        fclose(in);
    }

    fflush(out);
    fclose(out);

    chmod(tmp_path, 0600);
    chown(tmp_path, 0, 0);

    bool ok = auth_atomic_replace(tmp_path, PATH_SHADOW);

    auth_unlock_shadow(lock_fd);
    return ok;
}

bool auth_get_passwd_by_name(const char *username, passwd_entry_t *entry) {
    if (!username || !entry) return false;
    memset(entry, 0, sizeof(passwd_entry_t));

    FILE *f = fopen(PATH_PASSWD, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *ptr = line;
        char *token = strsep(&ptr, ":");
        if (!token || strcmp(token, username) != 0) continue;

        strncpy(entry->name, token, sizeof(entry->name) - 1);
        strsep(&ptr, ":");

        char *uid_str = strsep(&ptr, ":");
        entry->uid = (uid_str && *uid_str) ? (uid_t)atol(uid_str) : 0;

        char *gid_str = strsep(&ptr, ":");
        entry->gid = (gid_str && *gid_str) ? (gid_t)atol(gid_str) : 0;

        char *gecos = strsep(&ptr, ":");
        if (gecos) strncpy(entry->gecos, gecos, sizeof(entry->gecos) - 1);

        char *dir = strsep(&ptr, ":");
        if (dir) strncpy(entry->dir, dir, sizeof(entry->dir) - 1);

        char *shell = strsep(&ptr, ":");
        if (shell) strncpy(entry->shell, shell, sizeof(entry->shell) - 1);

        found = true;
        break;
    }

    fclose(f);
    return found;
}

bool auth_get_passwd_by_uid(uid_t uid, passwd_entry_t *entry) {
    if (!entry) return false;
    memset(entry, 0, sizeof(passwd_entry_t));

    FILE *f = fopen(PATH_PASSWD, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *ptr = line;
        char *token = strsep(&ptr, ":");
        if (!token) continue;
        char name[64];
        strncpy(name, token, sizeof(name) - 1);

        strsep(&ptr, ":");
        char *uid_str = strsep(&ptr, ":");
        if (!uid_str || (uid_t)atol(uid_str) != uid) continue;

        strncpy(entry->name, name, sizeof(entry->name) - 1);
        entry->uid = uid;

        char *gid_str = strsep(&ptr, ":");
        entry->gid = (gid_str && *gid_str) ? (gid_t)atol(gid_str) : 0;

        char *gecos = strsep(&ptr, ":");
        if (gecos) strncpy(entry->gecos, gecos, sizeof(entry->gecos) - 1);

        char *dir = strsep(&ptr, ":");
        if (dir) strncpy(entry->dir, dir, sizeof(entry->dir) - 1);

        char *shell = strsep(&ptr, ":");
        if (shell) strncpy(entry->shell, shell, sizeof(entry->shell) - 1);

        found = true;
        break;
    }

    fclose(f);
    return found;
}

bool auth_add_user(const passwd_entry_t *pw, const char *password) {
    if (!pw || !pw->name[0]) return false;

    int lock_fd = auth_lock_shadow();
    if (lock_fd < 0) return false;

    passwd_entry_t existing;
    if (auth_get_passwd_by_name(pw->name, &existing)) {
        auth_unlock_shadow(lock_fd);
        return false;
    }

    FILE *f = fopen(PATH_PASSWD, "a");
    if (!f) {
        auth_unlock_shadow(lock_fd);
        return false;
    }
    fprintf(f, "%s:x:%u:%u:%s:%s:%s\n",
            pw->name,
            (unsigned int)pw->uid,
            (unsigned int)pw->gid,
            pw->gecos[0] ? pw->gecos : pw->name,
            pw->dir[0] ? pw->dir : "/home",
            pw->shell[0] ? pw->shell : "/bin/bsh");
    fclose(f);
    chmod(PATH_PASSWD, 0644);

    char salt[32];
    sha512_crypt_gensalt(salt, sizeof(salt));
    char hash[130];
    if (password && password[0]) {
        sha512_crypt(password, salt, hash, sizeof(hash));
    } else {
        snprintf(hash, sizeof(hash), "!");
    }
    auth_update_shadow(pw->name, hash);

    group_entry_t grp;
    if (!auth_get_group_by_gid(pw->gid, &grp)) {
        FILE *gf = fopen(PATH_GROUP, "a");
        if (gf) {
            fprintf(gf, "%s:x:%u:\n", pw->name, (unsigned int)pw->gid);
            fclose(gf);
            chmod(PATH_GROUP, 0644);
        }
    }

    auth_unlock_shadow(lock_fd);
    return true;
}

bool auth_delete_user(const char *username) {
    if (!username) return false;

    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", PATH_PASSWD, getpid());
    FILE *out = fopen(tmp_path, "w");
    if (!out) return false;

    FILE *in = fopen(PATH_PASSWD, "r");
    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char line_copy[512];
            strncpy(line_copy, line, sizeof(line_copy) - 1);
            char *colon = strchr(line_copy, ':');
            if (colon) *colon = '\0';
            if (strcmp(line_copy, username) != 0) {
                fputs(line, out);
            }
        }
        fclose(in);
    }
    fclose(out);
    chmod(tmp_path, 0644);
    auth_atomic_replace(tmp_path, PATH_PASSWD);

    auth_delete_shadow(username);

    auth_remove_user_from_groups(username);

    return true;
}

bool auth_remove_user_from_groups(const char *username) {
    if (!username) return false;
    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", PATH_GROUP, getpid());
    FILE *out = fopen(tmp_path, "w");
    if (!out) return false;

    FILE *in = fopen(PATH_GROUP, "r");
    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (line[0] == '#' || line[0] == '\0') {
                fprintf(out, "%s\n", line);
                continue;
            }

            char line_copy[512];
            strncpy(line_copy, line, sizeof(line_copy) - 1);
            char *gptr = line_copy;
            char *gname = strsep(&gptr, ":");
            char *gpass = strsep(&gptr, ":");
            char *ggid  = strsep(&gptr, ":");
            char *gmem  = strsep(&gptr, ":");

            if (!gname || !ggid) {
                fprintf(out, "%s\n", line);
                continue;
            }

            fprintf(out, "%s:%s:%s:", gname, (gpass && *gpass) ? gpass : "x", ggid);
            if (gmem && gmem[0]) {
                char new_members[256] = {0};
                char *mptr = gmem;
                char *m;
                bool first = true;
                while ((m = strsep(&mptr, ",")) != NULL) {
                    while (*m == ' ') m++;
                    if (*m == '\0') continue;
                    if (strcmp(m, username) != 0) {
                        if (!first) strncat(new_members, ",", sizeof(new_members) - strlen(new_members) - 1);
                        strncat(new_members, m, sizeof(new_members) - strlen(new_members) - 1);
                        first = false;
                    }
                }
                fprintf(out, "%s\n", new_members);
            } else {
                fprintf(out, "\n");
            }
        }
        fclose(in);
    }
    fclose(out);
    chmod(tmp_path, 0644);
    return auth_atomic_replace(tmp_path, PATH_GROUP);
}

bool auth_modify_user(const char *username, uid_t new_uid, gid_t new_gid, const char *new_shell, const char *new_home) {
    if (!username) return false;

    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", PATH_PASSWD, getpid());
    FILE *out = fopen(tmp_path, "w");
    if (!out) return false;

    FILE *in = fopen(PATH_PASSWD, "r");
    bool found = false;
    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char line_copy[512];
            strncpy(line_copy, line, sizeof(line_copy) - 1);
            char *colon = strchr(line_copy, ':');
            if (colon) *colon = '\0';

            if (strcmp(line_copy, username) == 0) {
                passwd_entry_t pe;
                auth_get_passwd_by_name(username, &pe);
                if (new_uid != (uid_t)-1) pe.uid = new_uid;
                if (new_gid != (gid_t)-1) pe.gid = new_gid;
                if (new_shell && new_shell[0]) strncpy(pe.shell, new_shell, sizeof(pe.shell) - 1);
                if (new_home && new_home[0]) strncpy(pe.dir, new_home, sizeof(pe.dir) - 1);

                fprintf(out, "%s:x:%u:%u:%s:%s:%s\n",
                        pe.name, (unsigned int)pe.uid, (unsigned int)pe.gid,
                        pe.gecos, pe.dir, pe.shell);
                found = true;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }
    fclose(out);
    chmod(tmp_path, 0644);
    auth_atomic_replace(tmp_path, PATH_PASSWD);
    return found;
}

bool auth_get_group_by_name(const char *groupname, group_entry_t *entry) {
    if (!groupname || !entry) return false;
    memset(entry, 0, sizeof(group_entry_t));

    FILE *f = fopen(PATH_GROUP, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *ptr = line;
        char *token = strsep(&ptr, ":");
        if (!token || strcmp(token, groupname) != 0) continue;

        strncpy(entry->name, token, sizeof(entry->name) - 1);
        strsep(&ptr, ":");

        char *gid_str = strsep(&ptr, ":");
        entry->gid = (gid_str && *gid_str) ? (gid_t)atol(gid_str) : 0;

        char *members = strsep(&ptr, ":");
        if (members) strncpy(entry->members, members, sizeof(entry->members) - 1);

        found = true;
        break;
    }

    fclose(f);
    return found;
}

bool auth_get_group_by_gid(gid_t gid, group_entry_t *entry) {
    if (!entry) return false;
    memset(entry, 0, sizeof(group_entry_t));

    FILE *f = fopen(PATH_GROUP, "r");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *ptr = line;
        char *token = strsep(&ptr, ":");
        if (!token) continue;
        char name[64];
        strncpy(name, token, sizeof(name) - 1);

        strsep(&ptr, ":");
        char *gid_str = strsep(&ptr, ":");
        if (!gid_str || (gid_t)atol(gid_str) != gid) continue;

        strncpy(entry->name, name, sizeof(entry->name) - 1);
        entry->gid = gid;

        char *members = strsep(&ptr, ":");
        if (members) strncpy(entry->members, members, sizeof(entry->members) - 1);

        found = true;
        break;
    }

    fclose(f);
    return found;
}

bool auth_user_in_group(const char *username, const char *groupname) {
    if (!username || !groupname) return false;

    passwd_entry_t pe;
    if (auth_get_passwd_by_name(username, &pe)) {
        group_entry_t pri_grp;
        if (auth_get_group_by_gid(pe.gid, &pri_grp)) {
            if (strcmp(pri_grp.name, groupname) == 0) return true;
        }
    }

    group_entry_t grp;
    if (!auth_get_group_by_name(groupname, &grp)) return false;

    char mem_copy[256];
    strncpy(mem_copy, grp.members, sizeof(mem_copy) - 1);
    char *mptr = mem_copy;
    char *m;
    while ((m = strsep(&mptr, ",")) != NULL) {
        while (*m == ' ') m++;
        if (strcmp(m, username) == 0) return true;
    }
    return false;
}

bool auth_add_user_to_group(const char *username, const char *groupname) {
    if (!username || !groupname) return false;
    if (auth_user_in_group(username, groupname)) return true;

    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", PATH_GROUP, getpid());
    FILE *out = fopen(tmp_path, "w");
    if (!out) return false;

    FILE *in = fopen(PATH_GROUP, "r");
    bool found = false;
    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (line[0] == '#' || line[0] == '\0') {
                fprintf(out, "%s\n", line);
                continue;
            }

            char line_copy[512];
            strncpy(line_copy, line, sizeof(line_copy) - 1);
            char *gptr = line_copy;
            char *gname = strsep(&gptr, ":");
            char *gpass = strsep(&gptr, ":");
            char *ggid  = strsep(&gptr, ":");
            char *gmem  = strsep(&gptr, ":");

            if (gname && strcmp(gname, groupname) == 0) {
                found = true;
                const char *pass_str = (gpass && *gpass) ? gpass : "x";
                const char *gid_str = (ggid && *ggid) ? ggid : "0";
                if (gmem && gmem[0]) {
                    fprintf(out, "%s:%s:%s:%s,%s\n", gname, pass_str, gid_str, gmem, username);
                } else {
                    fprintf(out, "%s:%s:%s:%s\n", gname, pass_str, gid_str, username);
                }
            } else {
                fprintf(out, "%s\n", line);
            }
        }
        fclose(in);
    }
    fclose(out);
    chmod(tmp_path, 0644);
    auth_atomic_replace(tmp_path, PATH_GROUP);
    return found;
}

int auth_read_line(int fd, char *buf, size_t max_len, int echo_mode) {
    if (!buf || max_len == 0) return 0;
    size_t pos = 0;
    buf[0] = '\0';

    while (1) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;

        char ch = 0;
        int n = read(fd, &ch, 1);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            if (pos == 0) return -1;
            break;
        }

        if (ch == '\r' || ch == '\n') {
            if (ch == '\r') {
                struct pollfd pfd2 = { .fd = fd, .events = POLLIN, .revents = 0 };
                if (poll(&pfd2, 1, 10) > 0 && (pfd2.revents & POLLIN)) {
                    char next_ch = 0;
                    read(fd, &next_ch, 1);
                }
            }
            write(1, "\n", 1);
            break;
        }

        if (ch == 127 || ch == '\b') {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                if (echo_mode == 1 || echo_mode == 2) {
                    write(1, "\b \b", 3);
                }
            }
            continue;
        }

        if (ch == 3) {
            write(1, "^C\n", 3);
            buf[0] = '\0';
            return -1;
        }

        if (ch == 4) {
            if (pos == 0) return -1;
            break;
        }

        if ((unsigned char)ch >= 32 && pos + 1 < max_len) {
            buf[pos++] = ch;
            buf[pos] = '\0';
            if (echo_mode == 1) {
                write(1, &ch, 1);
            } else if (echo_mode == 2) {
                write(1, "*", 1);
            }
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

bool auth_read_password(const char *prompt, char *buf, size_t size) {
    if (!buf || size == 0) return false;

    struct pollfd pfd = { .fd = 0, .events = POLLIN, .revents = 0 };
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char dummy;
        if (read(0, &dummy, 1) <= 0) break;
    }

    if (prompt) {
        write(1, prompt, strlen(prompt));
    }
    int len = auth_read_line(0, buf, size, 0 /* silent */);
    if (len < 0) return false;
    return true;
}

uid_t auth_get_next_uid(uid_t min_id) {
    uid_t max_id = min_id;
    FILE *f = fopen(PATH_PASSWD, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (line[0] == '#' || line[0] == '\0') continue;

            char *ptr = line;
            strsep(&ptr, ":");
            strsep(&ptr, ":");
            char *u = strsep(&ptr, ":");
            if (u && *u) {
                uid_t cur = (uid_t)atol(u);
                if (cur >= max_id && cur < 60000) max_id = cur + 1;
            }
        }
        fclose(f);
    }
    return max_id;
}

gid_t auth_get_next_gid(gid_t min_id) {
    gid_t max_id = min_id;
    FILE *f = fopen(PATH_GROUP, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (line[0] == '#' || line[0] == '\0') continue;

            char *ptr = line;
            strsep(&ptr, ":");
            strsep(&ptr, ":");
            char *g = strsep(&ptr, ":");
            if (g && *g) {
                gid_t cur = (gid_t)atol(g);
                if (cur >= max_id && cur < 60000) max_id = cur + 1;
            }
        }
        fclose(f);
    }
    return max_id;
}

void auth_close_fds_ge_3(void) {
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd <= 0 || max_fd > 1024) max_fd = 64;
    for (int i = 3; i < (int)max_fd; i++) {
        close(i);
    }
}

static bool auth_copy_skel_tree(const char *src_dir, const char *dst_dir, uid_t uid, gid_t gid) {
    DIR *d = opendir(src_dir);
    if (!d) return false;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char src_path[256], dst_path[256];
        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, de->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, de->d_name);

        struct stat st;
        if (stat(src_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            mkdir(dst_path, 0755);
            chmod(dst_path, 0755);
            chown(dst_path, uid, gid);
            auth_copy_skel_tree(src_path, dst_path, uid, gid);
        } else if (S_ISREG(st.st_mode)) {
            int in_fd = open(src_path, O_RDONLY);
            if (in_fd >= 0) {
                int out_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd >= 0) {
                    char buf[1024];
                    ssize_t n;
                    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
                        write(out_fd, buf, n);
                    }
                    close(out_fd);
                    chmod(dst_path, 0644);
                    chown(dst_path, uid, gid);
                }
                close(in_fd);
            }
        }
    }
    closedir(d);
    return true;
}

bool auth_populate_skel(const char *homedir, uid_t uid, gid_t gid) {
    if (!homedir) return false;
    return auth_copy_skel_tree(PATH_SKEL, homedir, uid, gid);
}
