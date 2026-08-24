// Copyright (c) 2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ext4.h>
#include <ext4_mkfs.h>
#include <ext4_blockdev.h>
#include <ext4_errno.h>

#include <syscall.h>

#include <stdarg.h>
#include <errno.h>

static int s_fd = -1;
static uint8_t s_ph_buf[4096];

static int last_io_err = 0;
static uint64_t last_io_blk = 0;
static int64_t last_io_ret = 0;

static void log_error(const char *fmt, ...) {
    char buf[128];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    int fd = open("/tmp/mkfs_ext4.log", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        write(fd, buf, strlen(buf));
        close(fd);
    }
}

static int bdev_open(struct ext4_blockdev *bdev) {
    (void)bdev;
    return EOK;
}

static int bdev_bread(struct ext4_blockdev *bdev, void *buf,
                      uint64_t blk_id, uint32_t blk_cnt) {
    (void)bdev;
    off_t target = (off_t)blk_id * 512;
    off_t seek_res = lseek(s_fd, target, SEEK_SET);
    if (seek_res < 0) {
        last_io_err = 1;
        last_io_blk = blk_id;
        last_io_ret = (int64_t)seek_res;
        log_error("bread seek blk=%llu tgt=%lld res=%lld", (unsigned long long)blk_id, (long long)target, (long long)seek_res);
        return EIO;
    }
    size_t to_read = (size_t)blk_cnt * 512;
    size_t read_bytes = 0;
    uint8_t *p = (uint8_t *)buf;
    while (read_bytes < to_read) {
        ssize_t n = read(s_fd, p + read_bytes, to_read - read_bytes);
        if (n <= 0) {
            last_io_err = 2;
            last_io_blk = blk_id;
            last_io_ret = (int64_t)n;
            log_error("bread read blk=%llu n=%ld", (unsigned long long)blk_id, (long)n);
            return EIO;
        }
        read_bytes += (size_t)n;
    }
    return EOK;
}

static int bdev_bwrite(struct ext4_blockdev *bdev, const void *buf,
                       uint64_t blk_id, uint32_t blk_cnt) {
    (void)bdev;
    off_t target = (off_t)blk_id * 512;
    off_t seek_res = lseek(s_fd, target, SEEK_SET);
    if (seek_res < 0) {
        last_io_err = 3;
        last_io_blk = blk_id;
        last_io_ret = (int64_t)seek_res;
        log_error("bwrite seek blk=%llu tgt=%lld res=%lld err=%d", (unsigned long long)blk_id, (long long)target, (long long)seek_res, errno);
        return EIO;
    }
    size_t to_write = (size_t)blk_cnt * 512;
    size_t written = 0;
    const uint8_t *p = (const uint8_t *)buf;
    while (written < to_write) {
        ssize_t n = write(s_fd, p + written, to_write - written);
        if (n <= 0) {
            last_io_err = 4;
            last_io_blk = blk_id;
            last_io_ret = (int64_t)n;
            log_error("bwrite write blk=%llu n=%ld", (unsigned long long)blk_id, (long)n);
            return EIO;
        }
        written += (size_t)n;
    }
    return EOK;
}

static int bdev_close(struct ext4_blockdev *bdev) {
    (void)bdev;
    return EOK;
}

static int bdev_lock(struct ext4_blockdev *bdev) {
    (void)bdev;
    return EOK;
}

static int bdev_unlock(struct ext4_blockdev *bdev) {
    (void)bdev;
    return EOK;
}

int main(int argc, char **argv) {
    const char *devpath = NULL;
    const char *label = "BOREDOS";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (argv[i][0] != '-') {
            devpath = argv[i];
        }
    }

    if (!devpath) {
        printf("Usage: mkfs_ext4 [-L label] <device>\n");
        return 10;
    }

    char full_devpath[64];
    if (devpath[0] == '/') {
        strncpy(full_devpath, devpath, sizeof(full_devpath) - 1);
        full_devpath[sizeof(full_devpath) - 1] = '\0';
    } else {
        snprintf(full_devpath, sizeof(full_devpath), "/dev/%s", devpath);
    }

    s_fd = open(full_devpath, O_RDWR);
    if (s_fd < 0) {
        printf("Error: Could not open %s\n", full_devpath);
        return 20;
    }

    uint64_t total_sectors = 0;
    const char *dn = (full_devpath[0] == '/' && strncmp(full_devpath, "/dev/", 5) == 0) ? full_devpath + 5 : full_devpath;
    int n = sys_disk_get_count();
    for (int i = 0; i < n; i++) {
        disk_info_t d;
        if (sys_disk_get_info(i, &d) == 0 && strcmp(d.devname, dn) == 0) {
            total_sectors = d.total_sectors;
            break;
        }
    }

    off_t total_size = 0;
    if (total_sectors > 0) {
        total_size = (off_t)total_sectors * 512;
    } else {
        total_size = lseek(s_fd, 0, SEEK_END);
        lseek(s_fd, 0, SEEK_SET);
        if (total_size > 0) {
            total_sectors = (uint64_t)total_size / 512;
        }
    }

    if (total_size <= 0 || total_sectors == 0) {
        printf("Error: Could not determine size of %s\n", full_devpath);
        close(s_fd);
        return 30;
    }

    static uint8_t zero_buf[512 * 64];
    memset(zero_buf, 0, sizeof(zero_buf));
    lseek(s_fd, 0, SEEK_SET);
    write(s_fd, zero_buf, sizeof(zero_buf));
    lseek(s_fd, 0, SEEK_SET);

    struct ext4_blockdev_iface iface = {
        .open = bdev_open,
        .bread = bdev_bread,
        .bwrite = bdev_bwrite,
        .close = bdev_close,
        .lock = bdev_lock,
        .unlock = bdev_unlock,
        .ph_bsize = 512,
        .ph_bcnt = total_sectors,
        .ph_bbuf = s_ph_buf,
    };

    struct ext4_blockdev bd = {
        .bdif = &iface,
        .part_offset = 0,
        .part_size = (uint64_t)total_size,
    };

    struct ext4_fs fs;
    memset(&fs, 0, sizeof(fs));
    struct ext4_mkfs_info info;
    memset(&info, 0, sizeof(info));
    info.block_size = 4096;
    info.label = label;
    info.len = (uint64_t)total_size;

    int r = ext4_mkfs(&fs, &bd, &info, F_SET_EXT4);
    close(s_fd);

    if (r != EOK) {
        printf("mkfs_ext4 failed with error: %d, io_err: %d, blk: %llu, ret: %lld\n",
               r, last_io_err, (unsigned long long)last_io_blk, (long long)last_io_ret);
        if (last_io_err != 0) {
            return (100 + last_io_err);
        }
        return (40 + (r > 0 ? (r % 200) : 1));
    }

    return 0;
}
