// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <syscall.h>

int main(void) {
    syscall5(SYS_PRCTL, PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);

    while (1) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {
        }
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };
        nanosleep(&ts, NULL);
    }
}
