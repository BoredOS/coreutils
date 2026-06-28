// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <syscall.h>
#include <stdio.h>

typedef struct {
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
} pci_info_t;

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    FILE *f = fopen("/sys/bus/pci/devices", "r");
    if (!f) {
        printf("Error: Could not open /sys/bus/pci/devices\n");
        return 1;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        printf("%s", buf);
    }
    fclose(f);
    return 0;
}
