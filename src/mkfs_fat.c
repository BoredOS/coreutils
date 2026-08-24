// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>

#define SECTOR_SIZE 512

#pragma pack(push, 1)
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_short;
    uint8_t  media_type;
    uint16_t sectors_per_fat16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_long;

    // FAT32 Extended Fields
    uint32_t sectors_per_fat32;
    uint16_t extended_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
    uint8_t  boot_code[420];
    uint16_t boot_magic;
} fat32_vbr_t;

typedef struct {
    uint32_t lead_sig;       // 0x41615252
    uint8_t  reserved1[480];
    uint32_t struct_sig;     // 0x61417272
    uint32_t free_clusters;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;      // 0xAA550000
} fat32_fsinfo_t;

typedef struct {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} fat32_dir_entry_t;
#pragma pack(pop)

static uint8_t select_spc(uint32_t total_sectors) {
    if (total_sectors < 532480)   return 1;   // < 260 MB -> 512B
    if (total_sectors < 16777216) return 8;   // < 8 GB   -> 4 KB
    if (total_sectors < 33554432) return 16;  // < 16 GB  -> 8 KB
    if (total_sectors < 67108864) return 32;  // < 32 GB  -> 16 KB
    return 64;                                // >= 32 GB -> 32 KB
}

int main(int argc, char **argv) {
    const char *devpath = NULL;
    const char *label = "BOREDOS";
    int fat_type = 32;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            fat_type = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: mkfs.fat [OPTIONS] /dev/DEVICE\n");
            printf("  -F 32       FAT type (32 only)\n");
            printf("  -n LABEL    Volume label (max 11 chars, default: BOREDOS)\n");
            return 0;
        } else if (argv[i][0] != '-') {
            devpath = argv[i];
        }
    }

    if (!devpath) {
        printf("Usage: mkfs.fat -F 32 [-n LABEL] /dev/DEVICE\n");
        return 1;
    }
    if (fat_type != 32) {
        printf("[ERROR] Only FAT32 (-F 32) is supported.\n");
        return 1;
    }

    char full_devpath[64];
    if (devpath[0] != '/') {
        snprintf(full_devpath, sizeof(full_devpath), "/dev/%s", devpath);
    } else {
        strncpy(full_devpath, devpath, sizeof(full_devpath) - 1);
        full_devpath[sizeof(full_devpath) - 1] = '\0';
    }

    int fd = open(full_devpath, O_RDWR);
    if (fd < 0) {
        printf("[ERROR] Failed to open %s\n", full_devpath);
        return 1;
    }

    uint32_t total_sectors = 0;
    const char *devname = full_devpath;
    if (strncmp(devname, "/dev/", 5) == 0) devname += 5;

    int num_disks = sys_disk_get_count();
    for (int i = 0; i < num_disks; i++) {
        disk_info_t d;
        if (sys_disk_get_info(i, &d) == 0) {
            if (strcmp(d.devname, devname) == 0) {
                total_sectors = d.total_sectors;
                break;
            }
        }
    }

    if (total_sectors < 65536) {
        printf("[ERROR] Partition too small (< 32 MB) for FAT32 format.\n");
        close(fd);
        return 1;
    }

    printf("Formatting %s as FAT32 (label: %s, %u sectors)...\n", full_devpath, label, total_sectors);

    uint8_t spc = select_spc(total_sectors);
    uint16_t reserved_sectors = 32;
    uint8_t num_fats = 2;

    uint64_t bytes_per_cluster = (uint64_t)spc * SECTOR_SIZE;
    uint64_t data_sec = total_sectors - reserved_sectors;
    uint64_t fat_sectors = ((data_sec * 4) + (2 * bytes_per_cluster + 8 - 1)) / (bytes_per_cluster + 8);

    uint32_t total_clusters = (total_sectors - reserved_sectors - num_fats * (uint32_t)fat_sectors) / spc;

    fat32_vbr_t vbr;
    memset(&vbr, 0, sizeof(vbr));
    vbr.jmp[0] = 0xEB; vbr.jmp[1] = 0x58; vbr.jmp[2] = 0x90;
    memcpy(vbr.oem, "MSDOS5.0", 8);
    vbr.bytes_per_sector = SECTOR_SIZE;
    vbr.sectors_per_cluster = spc;
    vbr.reserved_sectors = reserved_sectors;
    vbr.num_fats = num_fats;
    vbr.root_entries = 0;
    vbr.total_sectors_short = 0;
    vbr.media_type = 0xF8;
    vbr.sectors_per_fat16 = 0;
    vbr.sectors_per_track = 32;
    vbr.num_heads = 64;
    vbr.hidden_sectors = 0;
    vbr.total_sectors_long = total_sectors;
    vbr.sectors_per_fat32 = (uint32_t)fat_sectors;
    vbr.extended_flags = 0;
    vbr.fs_version = 0;
    vbr.root_cluster = 2;
    vbr.fs_info_sector = 1;
    vbr.backup_boot_sector = 6;
    vbr.drive_number = 0x80;
    vbr.boot_signature = 0x29;
    vbr.volume_id = 0x20260821;
    memset(vbr.volume_label, ' ', 11);
    size_t llen = strlen(label);
    if (llen > 11) llen = 11;
    memcpy(vbr.volume_label, label, llen);
    memcpy(vbr.fs_type, "FAT32   ", 8);
    vbr.boot_magic = 0xAA55;

    fat32_fsinfo_t fsinfo;
    memset(&fsinfo, 0, sizeof(fsinfo));
    fsinfo.lead_sig = 0x41615252;
    fsinfo.struct_sig = 0x61417272;
    fsinfo.free_clusters = total_clusters > 1 ? (total_clusters - 1) : 0;
    fsinfo.next_free = 3;
    uint8_t zero_head[64 * SECTOR_SIZE];
    memset(zero_head, 0, sizeof(zero_head));
    lseek(fd, 0, SEEK_SET);
    write(fd, zero_head, sizeof(zero_head));

    lseek(fd, 0, SEEK_SET);
    write(fd, &vbr, sizeof(vbr));
    lseek(fd, 6 * SECTOR_SIZE, SEEK_SET);
    write(fd, &vbr, sizeof(vbr));

    lseek(fd, 1 * SECTOR_SIZE, SEEK_SET);
    write(fd, &fsinfo, sizeof(fsinfo));
    lseek(fd, 7 * SECTOR_SIZE, SEEK_SET);
    write(fd, &fsinfo, sizeof(fsinfo));

    uint8_t *fat_buf = (uint8_t*)malloc(SECTOR_SIZE);
    if (!fat_buf) {
        close(fd);
        return 1;
    }
    memset(fat_buf, 0, SECTOR_SIZE);
    uint32_t *fat_entries = (uint32_t*)fat_buf;
    fat_entries[0] = 0x0FFFFFF8;
    fat_entries[1] = 0x0FFFFFFF;
    fat_entries[2] = 0x0FFFFFFF;

    lseek(fd, (off_t)reserved_sectors * SECTOR_SIZE, SEEK_SET);
    write(fd, fat_buf, SECTOR_SIZE);

    lseek(fd, ((off_t)reserved_sectors + fat_sectors) * SECTOR_SIZE, SEEK_SET);
    write(fd, fat_buf, SECTOR_SIZE);

    memset(fat_buf, 0, SECTOR_SIZE);
    uint8_t *zero_chunk = (uint8_t*)calloc(1, 65536);
    if (zero_chunk) {
        uint64_t total_fat_bytes = (uint64_t)fat_sectors * SECTOR_SIZE;
        lseek(fd, (off_t)reserved_sectors * SECTOR_SIZE + SECTOR_SIZE, SEEK_SET);
        uint64_t rem = total_fat_bytes - SECTOR_SIZE;
        while (rem > 0) {
            size_t to_w = rem > 65536 ? 65536 : (size_t)rem;
            write(fd, zero_chunk, to_w);
            rem -= to_w;
        }
        lseek(fd, ((off_t)reserved_sectors + fat_sectors) * SECTOR_SIZE + SECTOR_SIZE, SEEK_SET);
        rem = total_fat_bytes - SECTOR_SIZE;
        while (rem > 0) {
            size_t to_w = rem > 65536 ? 65536 : (size_t)rem;
            write(fd, zero_chunk, to_w);
            rem -= to_w;
        }
        free(zero_chunk);
    }

    uint64_t root_lba = (uint64_t)reserved_sectors + (uint64_t)num_fats * fat_sectors;
    lseek(fd, (off_t)root_lba * SECTOR_SIZE, SEEK_SET);

    fat32_dir_entry_t vol_entry;
    memset(&vol_entry, 0, sizeof(vol_entry));
    memset(vol_entry.name, ' ', 11);
    memcpy(vol_entry.name, label, llen);
    vol_entry.attr = 0x08;
    write(fd, &vol_entry, sizeof(vol_entry));

    for (uint8_t c = 0; c < spc; c++) {
        if (c == 0) {
            uint8_t zero_rest[SECTOR_SIZE - sizeof(vol_entry)];
            memset(zero_rest, 0, sizeof(zero_rest));
            write(fd, zero_rest, sizeof(zero_rest));
        } else {
            write(fd, fat_buf, SECTOR_SIZE);
        }
    }

    free(fat_buf);
    close(fd);

    printf("Done.\n");
    return 0;
}
