// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef AUTH_SUBR_H
#define AUTH_SUBR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#define PATH_PASSWD      "/etc/passwd"
#define PATH_SHADOW      "/etc/shadow"
#define PATH_GROUP       "/etc/group"
#define PATH_SHADOW_LOCK "/etc/shadow.lock"
#define PATH_SHELLS      "/etc/shells"
#define PATH_SKEL        "/etc/skel"

typedef struct {
    char name[64];
    uid_t uid;
    gid_t gid;
    char gecos[64];
    char dir[128];
    char shell[64];
} passwd_entry_t;

typedef struct {
    char name[64];
    char hash[128];
    long lastchange;
    long min;
    long max;
    long warn;
    long inact;
    long expire;
    unsigned long flag;
} shadow_entry_t;

typedef struct {
    char name[64];
    gid_t gid;
    char members[256];
} group_entry_t;

int auth_lock_shadow(void);
void auth_unlock_shadow(int lock_fd);

bool auth_get_shadow(const char *username, shadow_entry_t *entry);
bool auth_update_shadow(const char *username, const char *new_hash);
bool auth_delete_shadow(const char *username);

bool auth_get_passwd_by_name(const char *username, passwd_entry_t *entry);
bool auth_get_passwd_by_uid(uid_t uid, passwd_entry_t *entry);
bool auth_add_user(const passwd_entry_t *pw, const char *password);
bool auth_delete_user(const char *username);
bool auth_modify_user(const char *username, uid_t new_uid, gid_t new_gid, const char *new_shell, const char *new_home);

bool auth_get_group_by_name(const char *groupname, group_entry_t *entry);
bool auth_get_group_by_gid(gid_t gid, group_entry_t *entry);
bool auth_add_user_to_group(const char *username, const char *groupname);
bool auth_user_in_group(const char *username, const char *groupname);
bool auth_remove_user_from_groups(const char *username);

int auth_read_line(int fd, char *buf, size_t max_len, int echo_mode);

bool auth_read_password(const char *prompt, char *buf, size_t size);

uid_t auth_get_next_uid(uid_t min_id);
gid_t auth_get_next_gid(gid_t min_id);

void auth_close_fds_ge_3(void);

bool auth_populate_skel(const char *homedir, uid_t uid, gid_t gid);

#endif // AUTH_SUBR_H
