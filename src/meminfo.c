// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <syscall.h>
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    FILE *f = fopen("/sys/kernel/meminfo", "r");
    if (!f) {
        printf("Error: Could not open /sys/kernel/meminfo\n");
        return 1;
    }
    char buf[64];
    uint64_t total = 0, used = 0;
    if (fgets(buf, sizeof(buf), f)) {
        total = strtoull(buf, NULL, 10);
    }
    if (fgets(buf, sizeof(buf), f)) {
        used = strtoull(buf, NULL, 10);
    }
    fclose(f);

    printf("Memory Info:\n");
    printf("Total: %d MB\n", (int)(total / 1024 / 1024));
    printf("Used:  %d MB\n", (int)(used / 1024 / 1024));
    printf("Free:  %d MB\n", (int)((total - used) / 1024 / 1024));
    return 0;
}
