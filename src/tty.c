// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    long tty_id = sys_system(SYSTEM_CMD_TTY_GET_ID, 0, 0, 0, 0);
    if (tty_id < 0) {
        printf("not a tty\n");
        return 1;
    }
    printf("/dev/tty%ld\n", tty_id + 1);
    return 0;
}
