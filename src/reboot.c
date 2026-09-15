// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdlib.h>
#include <syscall.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>

#define SIGUSR1 10

int main(int argc, char **argv) {
    bool force = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = true;
        }
    }

    if (getuid() != 0 && geteuid() != 0) {
        fprintf(stderr, "reboot: Permission denied (must be root, try 'doas reboot')\n");
        return 1;
    }

    if (force) {
        sys_reboot();
        return 0;
    }

    printf("Initiating system reboot...\n");
    if (sys_kill_signal(1, SIGUSR1) < 0) {
        sys_reboot();
        return 0;
    }

    for (int i = 0; i < 50; i++) {
        usleep(100 * 1000);
    }

    printf("Init did not reboot in time, forcing reboot...\n");
    sys_reboot();
    return 0;
}
