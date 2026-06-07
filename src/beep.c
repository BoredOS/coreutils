// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dev/pcsk.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int fd = open("/dev/pcsk", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/pcsk");
        return 1;
    }

    struct pcsk_beep b;
    b.freq = 392;
    b.ms = 400;

    if (ioctl(fd, PCSK_IOCTL_BEEP, &b) < 0) {
        perror("ioctl PCSK_IOCTL_BEEP");
        close(fd);
        return 1;
    }

    close(fd);
    printf("BEEP!\n");
    return 0;
}
