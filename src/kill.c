// Copyright (c) 2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Send a signal to a process.
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int is_numeric(const char *s) {
    if (!s || !s[0]) return 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
    }
    return 1;
}

static void print_usage(void) {
    printf("Usage: kill [-9] <pid>\n");
}

int main(int argc, char **argv) {
    int sig = SIGTERM;
    const char *pid_str = NULL;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-9") == 0) {
            sig = SIGKILL;
        } else if (!pid_str) {
            pid_str = argv[i];
        } else {
            print_usage();
            return 1;
        }
    }

    if (!pid_str || !is_numeric(pid_str)) {
        printf("kill: invalid pid: %s\n", pid_str ? pid_str : "(null)");
        return 1;
    }

    int pid = atoi(pid_str);
    if (pid <= 0) {
        printf("kill: invalid pid: %s\n", pid_str);
        return 1;
    }

    if (kill(pid, sig) != 0) {
        printf("kill: failed to signal pid %d (sig %d)\n", pid, sig);
        return 1;
    }

    return 0;
}
