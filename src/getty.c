// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "auth_subr.h"

int main(int argc, char **argv) {
    const char *autologin_user = NULL;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--autologin") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            autologin_user = argv[++i];
        }
    }

    if (autologin_user && autologin_user[0] && access("/etc/.configured", F_OK) != 0) {
        char *const login_args[] = { "/bin/login", "-f", (char *)autologin_user, NULL };
        execv("/bin/login", login_args);
        perror("exec /bin/login");
    }

    char username[64];
    while (1) {
        printf("boredos login: ");
        fflush(stdout);

        int len = auth_read_line(0, username, sizeof(username), 1 /* echo */);
        if (len < 0) {
            sleep(1);
            continue;
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

        if (username[0] == '\0') continue;

        char *const login_args[] = { "/bin/login", username, NULL };
        execv("/bin/login", login_args);

        perror("exec /bin/login");
        sleep(2);
    }

    return 0;
}
