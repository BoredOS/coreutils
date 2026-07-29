// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!isatty(0)) {
        printf("not a tty\n");
        return 1;
    }
    char *name = ttyname(0);
    if (!name) name = ptsname(0);
    if (name) {
        printf("%s\n", name);
    } else {
        printf("?\n");
        return 1;
    }
    return 0;
}
