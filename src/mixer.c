// Copyright (c) 2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/sound.h>

static void print_usage(void) {
    printf("Usage:\n");
    printf("  mixer                        Show current volume levels\n");
    printf("  mixer vol <0-100> [0-100]    Set master volume (L, or L R separately)\n");
    printf("  mixer pcm <0-100> [0-100]    Set PCM/DAC volume (L, or L R separately)\n");
    printf("  mixer vol +<n>               Raise master volume by n%%\n");
    printf("  mixer vol -<n>               Lower master volume by n%%\n");
    printf("  mixer pcm +<n>               Raise PCM volume by n%%\n");
    printf("  mixer pcm -<n>               Lower PCM volume by n%%\n");
}

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Pack two 0-100 channel values into the OSS mixer word.
static int pack_vol(int left, int right) {
    return (clamp(left, 0, 100) & 0xFF) | ((clamp(right, 0, 100) & 0xFF) << 8);
}

// Read current master or PCM level from the hardware, returning left channel.
static int read_left(int fd, unsigned long req) {
    int val = 0;
    ioctl(fd, req, &val);
    return val & 0xFF;
}

int main(int argc, char **argv) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage();
        return 0;
    }

    int fd = open("/dev/mixer", O_RDWR);
    if (fd < 0) {
        perror("mixer: cannot open /dev/mixer");
        return 1;
    }

    // No arguments: show current levels.
    if (argc == 1) {
        int vol = 0, pcm = 0;
        ioctl(fd, SOUND_MIXER_READ_VOLUME, &vol);
        ioctl(fd, SOUND_MIXER_READ_PCM,    &pcm);

        int vol_l = vol & 0xFF,        vol_r = (vol >> 8) & 0xFF;
        int pcm_l = pcm & 0xFF,        pcm_r = (pcm >> 8) & 0xFF;

        printf("Volume levels:\n");
        printf("  Master:  %d%% / %d%%\n", vol_l, vol_r);
        printf("  PCM:     %d%% / %d%%\n", pcm_l, pcm_r);

        close(fd);
        return 0;
    }

    // Determine which control to operate on.
    unsigned long read_req, write_req;
    const char *label;
    if (strcmp(argv[1], "vol") == 0) {
        read_req  = SOUND_MIXER_READ_VOLUME;
        write_req = SOUND_MIXER_WRITE_VOLUME;
        label     = "Master";
    } else if (strcmp(argv[1], "pcm") == 0) {
        read_req  = SOUND_MIXER_READ_PCM;
        write_req = SOUND_MIXER_WRITE_PCM;
        label     = "PCM";
    } else {
        fprintf(stderr, "mixer: unknown control '%s'\n", argv[1]);
        print_usage();
        close(fd);
        return 1;
    }

    if (argc < 3) {
        // No value given: just print the current level for that control.
        int val = 0;
        ioctl(fd, read_req, &val);
        printf("%s: %d%% / %d%%\n", label, val & 0xFF, (val >> 8) & 0xFF);
        close(fd);
        return 0;
    }

    const char *arg2 = argv[2];
    int left, right;

    if (arg2[0] == '+' || arg2[0] == '-') {
        // Relative adjustment: read current value and offset it.
        int delta = atoi(arg2); // sign is preserved by atoi
        int cur   = read_left(fd, read_req);
        left  = clamp(cur + delta, 0, 100);
        right = left;
    } else {
        // Absolute: one value sets both channels; an optional second sets the right.
        left  = clamp(atoi(arg2), 0, 100);
        right = (argc >= 4) ? clamp(atoi(argv[3]), 0, 100) : left;
    }

    int packed = pack_vol(left, right);
    if (ioctl(fd, write_req, &packed) < 0) {
        perror("mixer: ioctl write failed");
        close(fd);
        return 1;
    }

    // Read back to confirm what the hardware accepted.
    int confirmed = 0;
    ioctl(fd, read_req, &confirmed);
    printf("%s: %d%% / %d%%\n", label, confirmed & 0xFF, (confirmed >> 8) & 0xFF);

    close(fd);
    return 0;
}
