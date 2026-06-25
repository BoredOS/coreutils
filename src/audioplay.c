// Copyright (c) 2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/sound.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

typedef struct {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
    char fmt_chunk_marker[4];
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_chunk_header[4];
    uint32_t data_size;
} __attribute__((packed)) wav_header_t;

int play_wav(const char *filename) {
    int wav_fd = open(filename, O_RDONLY);
    if (wav_fd < 0) {
        perror("Failed to open WAV file");
        return 1;
    }

    wav_header_t header;
    if (read(wav_fd, &header, sizeof(header)) != sizeof(header)) {
        fprintf(stderr, "Failed to read WAV header\n");
        close(wav_fd);
        return 1;
    }

    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
        fprintf(stderr, "Not a valid RIFF/WAVE file\n");
        close(wav_fd);
        return 1;
    }

    printf("Playing WAV: %s\n", filename);
    printf("  Channels: %d\n", header.channels);
    printf("  Sample Rate: %d Hz\n", header.sample_rate);
    printf("  Bits per sample: %d\n", header.bits_per_sample);

    int dsp_fd = open("/dev/dsp", O_WRONLY);
    if (dsp_fd < 0) {
        perror("Failed to open /dev/dsp");
        close(wav_fd);
        return 1;
    }

    int fmt = AFMT_S16_LE;
    if (ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &fmt) < 0) perror("set format");

    int channels = header.channels;
    if (ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &channels) < 0) perror("set channels");

    int speed = header.sample_rate;
    if (ioctl(dsp_fd, SNDCTL_DSP_SPEED, &speed) < 0) perror("set speed");

    char buffer[4096];
    int bytes_read;
    while ((bytes_read = read(wav_fd, buffer, sizeof(buffer))) > 0) {
        if (header.channels == 1 && header.bits_per_sample == 16) {
            int samples = bytes_read / 2;
            int out_bytes = samples * 4;
            int16_t *src_buf = (int16_t*)buffer;
            int16_t *dst_buf = malloc(out_bytes);
            if (dst_buf) {
                for (int i = 0; i < samples; i++) {
                    dst_buf[i * 2] = src_buf[i];
                    dst_buf[i * 2 + 1] = src_buf[i];
                }
                write(dsp_fd, dst_buf, out_bytes);
                free(dst_buf);
            }
        } else if (header.channels == 2 && header.bits_per_sample == 16) {
            write(dsp_fd, buffer, bytes_read);
        } else {
            write(dsp_fd, buffer, bytes_read);
        }
    }

    ioctl(dsp_fd, SNDCTL_DSP_SYNC, NULL);

    close(dsp_fd);
    close(wav_fd);
    return 0;
}

int play_mp3(const char *filename) {
    int mp3_fd = open(filename, O_RDONLY);
    if (mp3_fd < 0) {
        perror("Failed to open MP3 file");
        return 1;
    }

    off_t size = lseek(mp3_fd, 0, SEEK_END);
    if (size <= 0) {
        fprintf(stderr, "Invalid MP3 file size\n");
        close(mp3_fd);
        return 1;
    }
    lseek(mp3_fd, 0, SEEK_SET);

    uint8_t *file_data = malloc(size);
    if (!file_data) {
        fprintf(stderr, "Out of memory allocating MP3 buffer\n");
        close(mp3_fd);
        return 1;
    }

    if (read(mp3_fd, file_data, size) != size) {
        fprintf(stderr, "Failed to read MP3 file\n");
        free(file_data);
        close(mp3_fd);
        return 1;
    }
    close(mp3_fd);

    int dsp_fd = open("/dev/dsp", O_WRONLY);
    if (dsp_fd < 0) {
        perror("Failed to open /dev/dsp");
        free(file_data);
        return 1;
    }

    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    mp3dec_frame_info_t info;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    uint8_t *mp3_ptr = file_data;
    int bytes_left = size;
    int speed = 0;

    printf("Playing MP3: %s\n", filename);

    while (bytes_left > 0) {
        int samples = mp3dec_decode_frame(&mp3d, mp3_ptr, bytes_left, pcm, &info);
        if (info.frame_bytes > 0) {
            mp3_ptr += info.frame_bytes;
            bytes_left -= info.frame_bytes;
        } else {
            mp3_ptr++;
            bytes_left--;
            continue;
        }

        if (samples > 0) {
            if (speed == 0) {
                speed = info.hz;
                printf("  Channels: %d\n", info.channels);
                printf("  Sample Rate: %d Hz\n", speed);

                int fmt = AFMT_S16_LE;
                if (ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &fmt) < 0) perror("set format");
                int chan = 2; 
                if (ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &chan) < 0) perror("set channels");
                int spd = speed;
                if (ioctl(dsp_fd, SNDCTL_DSP_SPEED, &spd) < 0) perror("set speed");
            }

            if (info.channels == 1) {
                int out_bytes = samples * 2 * sizeof(int16_t);
                int16_t *dst_buf = malloc(out_bytes);
                if (dst_buf) {
                    for (int i = 0; i < samples; i++) {
                        dst_buf[i * 2] = pcm[i];
                        dst_buf[i * 2 + 1] = pcm[i];
                    }
                    write(dsp_fd, dst_buf, out_bytes);
                    free(dst_buf);
                }
            } else {
                write(dsp_fd, pcm, samples * info.channels * sizeof(int16_t));
            }
        }
    }

    ioctl(dsp_fd, SNDCTL_DSP_SYNC, NULL);
    close(dsp_fd);
    free(file_data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("No WAV or MP3 file specified. Generating and playing a 440Hz test beep tone for 2 seconds...\n");
        int dsp_fd = open("/dev/dsp", O_WRONLY);
        if (dsp_fd < 0) {
            perror("Failed to open /dev/dsp");
            return 1;
        }

        int fmt = AFMT_S16_LE;
        if (ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &fmt) < 0) perror("set format");
        int channels = 2;
        if (ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &channels) < 0) perror("set channels");
        int speed = 48000;
        if (ioctl(dsp_fd, SNDCTL_DSP_SPEED, &speed) < 0) perror("set speed");

        int period = 48000 / 440;
        int samples = 48000 * 2;
        int16_t *buf = malloc(samples * 4);
        if (!buf) {
            fprintf(stderr, "Out of memory\n");
            close(dsp_fd);
            return 1;
        }

        for (int i = 0; i < samples; i++) {
            int16_t val = ((i / (period / 2)) % 2) ? 6000 : -6000;
            buf[i * 2] = val;
            buf[i * 2 + 1] = val;
        }

        write(dsp_fd, buf, samples * 4);
        ioctl(dsp_fd, SNDCTL_DSP_SYNC, NULL);
        free(buf);
        close(dsp_fd);
        printf("Done playing test tone.\n");
        return 0;
    }

    int len = strlen(argv[1]);
    if (len >= 4 && strcasecmp(argv[1] + len - 4, ".mp3") == 0) {
        return play_mp3(argv[1]);
    } else {
        return play_wav(argv[1]);
    }
}
