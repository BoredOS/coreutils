// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <syscall.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#define MAX_PARTS 4
#define SECTOR_SIZE_BYTES 512ULL
#define ONE_MB (1024ULL * 1024ULL)
#define ONE_GB (1024ULL * 1024ULL * 1024ULL)

#define GPT_PART_ENTRY_COUNT 128
#define GPT_PART_ENTRY_SIZE  128

#pragma pack(push, 1)
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_array_crc32;
    uint8_t  reserved_padding[420];
} gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_partition_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36];
} gpt_entry_t;
#pragma pack(pop)

static const uint8_t ESP_GUID[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
};

static const uint8_t BASIC_DATA_GUID[16] = {
    0xaf, 0x3d, 0xcb, 0xeb, 0x0f, 0x5f, 0xb7, 0x44,
    0x8e, 0x7f, 0x6a, 0x56, 0xfb, 0x22, 0x93, 0x71
};

static uint32_t crc32_tab[256];
static int crc32_tab_inited = 0;

static void crc32_init_table(void) {
    if (crc32_tab_inited) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        crc32_tab[i] = c;
    }
    crc32_tab_inited = 1;
}

static uint32_t calc_crc32(const void *buf, size_t len) {
    crc32_init_table();
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_tab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static int user_write_mbr(int fd, uint32_t total_sectors, partition_spec_t *parts, int count) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    for (int i = 0; i < count && i < 4; i++) {
        if (parts[i].sector_count == 0) return -1;
        uint8_t *entry = buf + 446 + i * 16;
        entry[0] = (i == 0) ? 0x80 : 0x00;
        entry[4] = (parts[i].flags & PART_FLAG_ESP) ? 0xEF : 0x0C;
        uint32_t start = parts[i].lba_start;
        uint32_t scnt = parts[i].sector_count;
        entry[8]  = (uint8_t)(start);
        entry[9]  = (uint8_t)(start >> 8);
        entry[10] = (uint8_t)(start >> 16);
        entry[11] = (uint8_t)(start >> 24);
        entry[12] = (uint8_t)(scnt);
        entry[13] = (uint8_t)(scnt >> 8);
        entry[14] = (uint8_t)(scnt >> 16);
        entry[15] = (uint8_t)(scnt >> 24);
    }

    buf[510] = 0x55;
    buf[511] = 0xAA;

    lseek(fd, 0, SEEK_SET);
    if (write(fd, buf, 512) != 512) return -1;
    return 0;
}

static int user_write_gpt(int fd, uint32_t total_sectors, partition_spec_t *parts, int count) {
    if (total_sectors < 68) return -1;

    uint8_t pmbr[512];
    memset(pmbr, 0, sizeof(pmbr));
    pmbr[446] = 0x00;
    pmbr[447] = 0x00; pmbr[448] = 0x02; pmbr[449] = 0x00;
    pmbr[450] = 0xEE;
    pmbr[451] = 0xFF; pmbr[452] = 0xFF; pmbr[453] = 0xFF;
    pmbr[454] = 0x01;
    uint32_t pmbr_size = total_sectors - 1;
    pmbr[458] = (uint8_t)(pmbr_size);
    pmbr[459] = (uint8_t)(pmbr_size >> 8);
    pmbr[460] = (uint8_t)(pmbr_size >> 16);
    pmbr[461] = (uint8_t)(pmbr_size >> 24);
    pmbr[510] = 0x55;
    pmbr[511] = 0xAA;

    lseek(fd, 0, SEEK_SET);
    if (write(fd, pmbr, 512) != 512) return -1;

    uint8_t *entries = (uint8_t*)calloc(GPT_PART_ENTRY_COUNT, GPT_PART_ENTRY_SIZE);
    if (!entries) return -1;

    for (int i = 0; i < count && i < GPT_PART_ENTRY_COUNT; i++) {
        gpt_entry_t *e = (gpt_entry_t*)(entries + i * GPT_PART_ENTRY_SIZE);
        if (parts[i].flags & PART_FLAG_ESP) {
            memcpy(e->type_guid, ESP_GUID, 16);
        } else {
            memcpy(e->type_guid, BASIC_DATA_GUID, 16);
        }
        for (int b = 0; b < 16; b++) e->unique_partition_guid[b] = (uint8_t)(0x30 + i + b);
        e->starting_lba = parts[i].lba_start;
        e->ending_lba = parts[i].lba_start + parts[i].sector_count - 1;
        e->attributes = 0;
        for (int c = 0; parts[i].label[c] && c < 35; c++) {
            e->partition_name[c] = (uint16_t)parts[i].label[c];
        }
    }

    uint32_t entry_crc = calc_crc32(entries, GPT_PART_ENTRY_COUNT * GPT_PART_ENTRY_SIZE);

    gpt_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.signature = 0x5452415020494645ULL;
    hdr.revision = 0x00010000;
    hdr.header_size = 92;
    hdr.my_lba = 1;
    hdr.alternate_lba = total_sectors - 1;
    hdr.first_usable_lba = 34;
    hdr.last_usable_lba = total_sectors - 34;
    for (int b = 0; b < 16; b++) hdr.disk_guid[b] = (uint8_t)(0x10 + b);
    hdr.partition_entry_lba = 2;
    hdr.num_partition_entries = GPT_PART_ENTRY_COUNT;
    hdr.size_of_partition_entry = GPT_PART_ENTRY_SIZE;
    hdr.partition_entry_array_crc32 = entry_crc;
    hdr.crc32 = calc_crc32(&hdr, hdr.header_size);

    lseek(fd, 1 * 512, SEEK_SET);
    write(fd, &hdr, 512);

    lseek(fd, 2 * 512, SEEK_SET);
    write(fd, entries, GPT_PART_ENTRY_COUNT * GPT_PART_ENTRY_SIZE);

    lseek(fd, (off_t)(total_sectors - 33) * 512, SEEK_SET);
    write(fd, entries, GPT_PART_ENTRY_COUNT * GPT_PART_ENTRY_SIZE);

    gpt_header_t bhdr = hdr;
    bhdr.my_lba = total_sectors - 1;
    bhdr.alternate_lba = 1;
    bhdr.partition_entry_lba = total_sectors - 33;
    bhdr.crc32 = 0;
    bhdr.crc32 = calc_crc32(&bhdr, bhdr.header_size);

    lseek(fd, (off_t)(total_sectors - 1) * 512, SEEK_SET);
    write(fd, &bhdr, 512);

    free(entries);
    return 0;
}

static void print_usage(void) {
    printf("fdisk [OPTIONS] /dev/DEVICE\n");
    printf("  -p, --print     Print partition table and exit\n");
    printf("  -s, --script    Non-interactive auto-partition\n");
    printf("      --mbr       Use MBR instead of GPT\n");
    printf("      --uefi      Include ESP (default with GPT)\n");
    printf("      --esp-size N  ESP size (b/mb/gb, default: 512mb)\n");
    printf("  -h, --help\n");
}

static int sc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sc_atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

static char sc_tolower(char ch) {
    if (ch >= 'A' && ch <= 'Z') return (char)(ch + 32);
    return ch;
}

static uint64_t sc_parse_size_bytes(const char *s) {
    uint64_t n = 0;
    int has_digit = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') {
        has_digit = 1;
        n = n * 10ULL + (uint64_t)(*s - '0');
        s++;
    }

    if (!has_digit) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return n * ONE_MB;

    char c1 = sc_tolower(*s);
    char c2 = sc_tolower(*(s + 1));
    if (c1 == 'g' && c2 == 'b') return n * ONE_GB;
    if (c1 == 'm' && c2 == 'b') return n * ONE_MB;
    if (c1 == 'b') return n;
    if (c1 == 'g' && c2 == '\0') return n * ONE_GB;
    if (c1 == 'm' && c2 == '\0') return n * ONE_MB;

    return n * ONE_MB;
}

static uint32_t sc_bytes_to_sectors_ceil(uint64_t bytes) {
    if (bytes == 0) return 0;
    return (uint32_t)((bytes + SECTOR_SIZE_BYTES - 1ULL) / SECTOR_SIZE_BYTES);
}

static void sc_format_size(uint64_t bytes, char *out, size_t out_len) {
    if (bytes >= ONE_GB) {
        uint64_t gb = bytes / ONE_GB;
        uint64_t rem = (bytes % ONE_GB) * 10ULL / ONE_GB;
        snprintf(out, out_len, "%llu.%llu GB", (unsigned long long)gb, (unsigned long long)rem);
    } else if (bytes >= ONE_MB) {
        uint64_t mb = bytes / ONE_MB;
        uint64_t rem = (bytes % ONE_MB) * 10ULL / ONE_MB;
        snprintf(out, out_len, "%llu.%llu MB", (unsigned long long)mb, (unsigned long long)rem);
    } else {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    }
}

static void print_partition_table(const char *devname) {
    int n = sys_disk_get_count();
    int found = 0;
    printf("Partition table for /dev/%s:\n", devname);
    printf("%-10s %-12s %-12s %-10s %-6s %s\n",
           "Device", "Start", "End", "Size", "ESP", "FAT32");
    for (int i = 0; i < n; i++) {
        disk_info_t d;
        if (sys_disk_get_info(i, &d) != 0) continue;
        if (!d.is_partition) continue;
        int len = 0;
        while (devname[len]) len++;
        int match = 1;
        for (int j = 0; j < len; j++) {
            if (d.devname[j] != devname[j]) { match = 0; break; }
        }
        if (!match) continue;
        char start_buf[24], end_buf[24], size_buf[24];
        uint64_t start_bytes = (uint64_t)d.lba_offset * SECTOR_SIZE_BYTES;
        uint64_t end_bytes = (uint64_t)(d.lba_offset + d.total_sectors - 1) * SECTOR_SIZE_BYTES;
        uint64_t size_bytes = (uint64_t)d.total_sectors * SECTOR_SIZE_BYTES;
        sc_format_size(start_bytes, start_buf, sizeof(start_buf));
        sc_format_size(end_bytes, end_buf, sizeof(end_buf));
        sc_format_size(size_bytes, size_buf, sizeof(size_buf));
        printf("/dev/%-5s %-12s %-12s %-10s %-6s %s\n",
               d.devname, start_buf, end_buf, size_buf,
               d.is_esp ? "yes" : "no",
               d.is_fat32 ? "yes" : "no");
        found++;
    }
    if (!found) printf("  (no partitions)\n");
}

int main(int argc, char **argv) {
    int opt_print   = 0;
    int opt_script  = 0;
    int opt_mbr     = 0;
    int opt_uefi    = 1;
    uint64_t esp_size_bytes = 512ULL * ONE_MB;
    const char *devname = NULL;

    for (int i = 1; i < argc; i++) {
        if (sc_strcmp(argv[i], "-p") == 0 || sc_strcmp(argv[i], "--print") == 0)
            opt_print = 1;
        else if (sc_strcmp(argv[i], "-s") == 0 || sc_strcmp(argv[i], "--script") == 0)
            opt_script = 1;
        else if (sc_strcmp(argv[i], "--mbr") == 0) { opt_mbr = 1; opt_uefi = 0; }
        else if (sc_strcmp(argv[i], "--uefi") == 0) opt_uefi = 1;
        else if (sc_strcmp(argv[i], "--esp-size") == 0 && i + 1 < argc) {
            uint64_t parsed = sc_parse_size_bytes(argv[++i]);
            if (parsed > 0) esp_size_bytes = parsed;
        }
        else if (sc_strcmp(argv[i], "-h") == 0 || sc_strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] != '-') {
            devname = argv[i];
            if (devname[0]=='/' && devname[1]=='d' && devname[2]=='e' && devname[3]=='v' && devname[4]=='/')
                devname += 5;
        }
    }

    if (!devname) { print_usage(); return 1; }

    if (opt_print) {
        print_partition_table(devname);
        return 0;
    }

    disk_info_t disk;
    int found = 0;
    int n = sys_disk_get_count();
    for (int i = 0; i < n; i++) {
        disk_info_t d;
        if (sys_disk_get_info(i, &d) != 0) continue;
        if (!d.is_partition && sc_strcmp(d.devname, devname) == 0) {
            disk = d;
            found = 1;
            break;
        }
    }
    if (!found) { printf("[ERROR] Device not found: /dev/%s\n", devname); return 1; }

    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/%s", devname);
    int fd = open(devpath, O_RDWR);
    if (fd < 0) {
        printf("[ERROR] Failed to open %s for partitioning.\n", devpath);
        return 1;
    }

    partition_spec_t parts[2];
    int count = 0;
    int ret = 0;

    if (!opt_mbr && opt_uefi) {
        uint32_t esp_sectors = sc_bytes_to_sectors_ceil(esp_size_bytes);
        if (esp_sectors % 2048) esp_sectors = ((esp_sectors + 2047) / 2048) * 2048;
        parts[0].lba_start    = 2048;
        parts[0].sector_count = esp_sectors;
        parts[0].part_type    = 0;
        parts[0].flags        = PART_FLAG_ESP;
        strcpy(parts[0].label, "EFI System");

        uint32_t root_start = 2048 + esp_sectors;
        if (root_start % 2048) root_start = ((root_start + 2047) / 2048) * 2048;
        parts[1].lba_start    = root_start;
        parts[1].sector_count = disk.total_sectors - root_start - 34;
        parts[1].part_type    = 0;
        parts[1].flags        = 0;
        strcpy(parts[1].label, "BoredOS");
        count = 2;
        ret = user_write_gpt(fd, disk.total_sectors, parts, count);
    } else {
        parts[0].lba_start    = 2048;
        parts[0].sector_count = disk.total_sectors - 2048;
        parts[0].part_type    = 0;
        parts[0].flags        = 0;
        strcpy(parts[0].label, "BoredOS");
        count = 1;
        ret = user_write_mbr(fd, disk.total_sectors, parts, count);
    }

    close(fd);

    if (ret != 0) { printf("[ERROR] Partition write failed.\n"); return 1; }
    printf("Partition table written to /dev/%s.\n", devname);

    sys_disk_rescan(devname);
    return 0;
}
