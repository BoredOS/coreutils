// Copyright (c) 2026 Lluciocc (https://github.com/lluciocc)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// BOREDOS_APP_DESC: Change keyboard layout.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>

#define KEYMAP_QWERTY 0
#define KEYMAP_AZERTY 1
#define KEYMAP_QWERTZ 2
#define KEYMAP_DVORAK 3

// Convert a layout name to its corresponding ID, or return -1 if unknown.
static int name_to_id(const char *name) {
    if (!name) return -1;
    if (strcmp(name, "qwerty") == 0 || strcmp(name, "us") == 0) return KEYMAP_QWERTY;
    if (strcmp(name, "azerty") == 0 || strcmp(name, "fr") == 0) return KEYMAP_AZERTY;
    if (strcmp(name, "qwertz") == 0 || strcmp(name, "de") == 0) return KEYMAP_QWERTZ;
    if (strcmp(name, "dvorak") == 0) return KEYMAP_DVORAK;
    char *end;
    long v = strtol(name, &end, 10);
    if (end != name && v >= 0 && v <= 255) return (int)v;
    return -1;
}

// Convert a layout ID to its corresponding name, or "UNKNOWN" if unknown.
static const char *id_to_name(int id) {
    switch (id) {
        case KEYMAP_QWERTY: return "QWERTY";
        case KEYMAP_AZERTY: return "AZERTY";
        case KEYMAP_QWERTZ: return "QWERTZ";
        case KEYMAP_DVORAK: return "DVORAK";
        default: return "UNKNOWN";
    }
}

int main(int argc, char **argv) {
    (void)argc;
    if (argc < 2) {
        int cur = sys_system(SYSTEM_CMD_GET_KEYBOARD_LAYOUT, 0, 0, 0, 0);
        printf("Current keyboard layout: %s (%d)\n", id_to_name(cur), cur);
        return 0;
    }

    const char *arg = argv[1];
    int id = name_to_id(arg);
    if (id < 0) {
        fprintf(stderr, "Unknown layout '%s'\n", arg);
        fprintf(stderr, "Supported: qwerty, us, azerty, fr, qwertz, de, dvorak, or numeric id\n");
        return 1;
    }

    sys_system(SYSTEM_CMD_SET_KEYBOARD_LAYOUT, (uint64_t)id, 0, 0, 0);
    printf("Keyboard layout set to %s (%d)\n", id_to_name(id), id);
    return 0;
}
