// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "auth_subr.h"
#include "libcrypt_sha512.h"

int main(int argc, char **argv) {
    uid_t ruid = getuid();
    char target_user[64] = {0};

    if (argc > 1) {
        if (ruid != 0) {
            fprintf(stderr, "passwd: Only root may specify a user name.\n");
            return 1;
        }
        strncpy(target_user, argv[1], sizeof(target_user) - 1);
    } else {
        passwd_entry_t current_pw;
        if (!auth_get_passwd_by_uid(ruid, &current_pw)) {
            fprintf(stderr, "passwd: Cannot identify current user (uid %u)\n", (unsigned int)ruid);
            return 1;
        }
        strncpy(target_user, current_pw.name, sizeof(target_user) - 1);
    }

    passwd_entry_t pw;
    if (!auth_get_passwd_by_name(target_user, &pw)) {
        fprintf(stderr, "passwd: User '%s' does not exist.\n", target_user);
        return 1;
    }

    shadow_entry_t sh;
    bool has_shadow = auth_get_shadow(target_user, &sh);

    if (ruid != 0) {
        char old_pass[128];
        if (!auth_read_password("Current password: ", old_pass, sizeof(old_pass))) {
            return 1;
        }

        bool old_ok = false;
        if (has_shadow && sh.hash[0] != '\0' && sh.hash[0] != '!' && sh.hash[0] != '*') {
            old_ok = sha512_crypt_verify(old_pass, sh.hash);
        } else if (has_shadow && sh.hash[0] == '\0') {
            old_ok = (old_pass[0] == '\0');
        }
        memset(old_pass, 0, sizeof(old_pass));

        if (!old_ok) {
            fprintf(stderr, "passwd: Authentication failure\n");
            return 1;
        }
    }

    char new_pass1[128];
    char new_pass2[128];

    if (!auth_read_password("New password: ", new_pass1, sizeof(new_pass1))) {
        return 1;
    }

    if (strlen(new_pass1) < 4 && ruid != 0) {
        fprintf(stderr, "passwd: Password must be at least 4 characters.\n");
        memset(new_pass1, 0, sizeof(new_pass1));
        return 1;
    }

    if (!auth_read_password("Retype new password: ", new_pass2, sizeof(new_pass2))) {
        memset(new_pass1, 0, sizeof(new_pass1));
        return 1;
    }

    if (strcmp(new_pass1, new_pass2) != 0) {
        fprintf(stderr, "passwd: Passwords do not match.\n");
        memset(new_pass1, 0, sizeof(new_pass1));
        memset(new_pass2, 0, sizeof(new_pass2));
        return 1;
    }

    char salt[32];
    sha512_crypt_gensalt(salt, sizeof(salt));

    char new_hash[130];
    if (!sha512_crypt(new_pass1, salt, new_hash, sizeof(new_hash))) {
        fprintf(stderr, "passwd: Hash computation failed.\n");
        memset(new_pass1, 0, sizeof(new_pass1));
        memset(new_pass2, 0, sizeof(new_pass2));
        return 1;
    }

    memset(new_pass1, 0, sizeof(new_pass1));
    memset(new_pass2, 0, sizeof(new_pass2));

    if (!auth_update_shadow(target_user, new_hash)) {
        fprintf(stderr, "passwd: Failed to update shadow file.\n");
        return 1;
    }

    printf("passwd: password updated successfully\n");
    return 0;
}
