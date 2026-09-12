// Copyright (c) 2026 zeyadhost (https://github.com/zeyadhost)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define LSBLK_MAX_DISKS 32
#define LSBLK_SECTOR_SIZE 512ULL
#define LSBLK_KB 1024ULL
#define LSBLK_MB (1024ULL * 1024ULL)
#define LSBLK_GB (1024ULL * 1024ULL * 1024ULL)

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static const char *display_label(const disk_info_t *d) {
    if (!d->label[0]) return "";
    if (streq(d->label, "Unknown Partition")) return "";
    if (streq(d->label, "FAT32 Partition")) return "";
    if (streq(d->label, "EFI System Partition")) return "EFI";
    return d->label;
}

static const char *device_name_arg(const char *arg) {
    if (starts_with(arg, "/dev/")) return arg + 5;
    return arg;
}

static void format_size(uint64_t bytes, char *out, size_t out_len, int compact) {
    const char *sep = compact ? "" : " ";
    uint64_t unit = 1;
    const char *suffix = "B";

    if (bytes >= LSBLK_GB) {
        unit = LSBLK_GB;
        suffix = "GB";
    } else if (bytes >= LSBLK_MB) {
        unit = LSBLK_MB;
        suffix = "MB";
    } else if (bytes >= LSBLK_KB) {
        unit = LSBLK_KB;
        suffix = "KB";
    }

    if (unit == 1) {
        snprintf(out, out_len, "%llu%s%s", (unsigned long long)bytes, sep, suffix);
        return;
    }

    uint64_t whole = bytes / unit;
    uint64_t rem = bytes % unit;
    uint64_t tenth = (rem * 10ULL + unit / 2ULL) / unit;

    if (tenth >= 10ULL) {
        whole++;
        tenth = 0;
    }

    if (tenth == 0) {
        snprintf(out, out_len, "%llu%s%s", (unsigned long long)whole, sep, suffix);
    } else {
        snprintf(out, out_len, "%llu.%llu%s%s", (unsigned long long)whole, (unsigned long long)tenth, sep, suffix);
    }
}

static uint64_t disk_size_bytes(const disk_info_t *d) {
    return (uint64_t)d->total_sectors * LSBLK_SECTOR_SIZE;
}

static int is_child_partition(const disk_info_t *disk, const disk_info_t *part) {
    size_t len;

    if (disk->is_partition || !part->is_partition) return 0;

    len = strlen(disk->devname);
    if (strncmp(part->devname, disk->devname, len) != 0) return 0;

    return part->devname[len] >= '0' && part->devname[len] <= '9';
}

static int child_count(const disk_info_t *disk, disk_info_t *items, int count) {
    int children = 0;

    for (int i = 0; i < count; i++) {
        if (is_child_partition(disk, &items[i])) children++;
    }

    return children;
}

static void print_tree_device(const disk_info_t *d, const char *branch) {
    char size[24];
    const char *type = d->is_partition ? "part" : "disk";

    format_size(disk_size_bytes(d), size, sizeof(size), 0);

    if (d->is_partition) {
        const char *label = display_label(d);
        if (branch[0]) printf("%s %-8s %8s  %s", branch, d->devname, size, type);
        else printf("/dev/%-8s %8s  %s", d->devname, size, type);
        if (d->is_fat32) printf("  FAT32");
        if (label[0]) printf("  %s", label);
        if (d->is_esp) printf("  [ESP]");
        printf("\n");
    } else {
        printf("/dev/%-8s %8s  %s\n", d->devname, size, type);
    }
}

static void print_tree_disk(const disk_info_t *disk, disk_info_t *items, int count) {
    int children = child_count(disk, items, count);
    int seen = 0;

    print_tree_device(disk, "");

    for (int i = 0; i < count; i++) {
        if (!is_child_partition(disk, &items[i])) continue;
        seen++;
        print_tree_device(&items[i], seen == children ? "└─" : "├─");
    }
}

static void print_raw_device(const disk_info_t *d) {
    char size[24];

    format_size(disk_size_bytes(d), size, sizeof(size), 1);
    printf("/dev/%s %s %s", d->devname, size, d->is_partition ? "part" : "disk");

    if (d->is_partition) {
        const char *label = display_label(d);
        if (d->is_fat32) printf(" FAT32");
        if (label[0]) printf(" %s", label);
        if (d->is_esp) printf(" ESP");
    }

    printf("\n");
}

static void print_raw_disk(const disk_info_t *disk, disk_info_t *items, int count) {
    print_raw_device(disk);

    for (int i = 0; i < count; i++) {
        if (is_child_partition(disk, &items[i])) print_raw_device(&items[i]);
    }
}

static void json_string(const char *s) {
    putchar('"');

    while (*s) {
        if (*s == '"' || *s == '\\') {
            putchar('\\');
            putchar(*s);
        } else if (*s == '\n') {
            printf("\\n");
        } else {
            putchar(*s);
        }
        s++;
    }

    putchar('"');
}

static void json_device_fields(const disk_info_t *d) {
    char size[24];
    char name[24];

    format_size(disk_size_bytes(d), size, sizeof(size), 0);
    snprintf(name, sizeof(name), "/dev/%s", d->devname);

    printf("\"name\":");
    json_string(name);
    printf(",\"size\":");
    json_string(size);
    printf(",\"type\":");
    json_string(d->is_partition ? "part" : "disk");
    printf(",\"fstype\":");
    json_string(d->is_fat32 ? "FAT32" : "");
    printf(",\"label\":");
    json_string(display_label(d));
    printf(",\"flags\":[");
    if (d->is_esp) json_string("ESP");
    printf("]");
}

static void print_json_partition(const disk_info_t *d) {
    printf("{");
    json_device_fields(d);
    printf("}");
}

static void print_json_disk(const disk_info_t *disk, disk_info_t *items, int count) {
    int seen = 0;

    printf("{");
    json_device_fields(disk);
    printf(",\"children\":[");

    for (int i = 0; i < count; i++) {
        if (!is_child_partition(disk, &items[i])) continue;
        if (seen > 0) printf(",");
        print_json_partition(&items[i]);
        seen++;
    }

    printf("]}");
}

static const uint8_t ESP_GUID[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
};

static bool disk_partition_is_esp(const char *parent_dev, int part_num) {
    if (part_num <= 0) return false;
    char path[128];
    snprintf(path, sizeof(path), "/dev/%s", parent_dev);
    int pfd = open(path, O_RDONLY);
    if (pfd < 0) return false;

    uint8_t sec[512];
    bool esp = false;

    if (lseek(pfd, 512, SEEK_SET) == 512 && read(pfd, sec, 512) == 512) {
        if (memcmp(sec, "EFI PART", 8) == 0) {
            uint64_t part_lba = *(uint64_t*)(sec + 72);
            uint32_t num_parts = *(uint32_t*)(sec + 80);
            uint32_t part_sz = *(uint32_t*)(sec + 84);
            if (part_sz >= 128 && (uint32_t)part_num <= num_parts) {
                off_t offset = (off_t)part_lba * 512 + (off_t)(part_num - 1) * part_sz;
                uint8_t entry_guid[16];
                if (lseek(pfd, offset, SEEK_SET) == offset && read(pfd, entry_guid, 16) == 16) {
                    if (memcmp(entry_guid, ESP_GUID, 16) == 0) {
                        esp = true;
                    }
                }
            }
            close(pfd);
            return esp;
        }
    }

    if (lseek(pfd, 0, SEEK_SET) == 0 && read(pfd, sec, 512) == 512) {
        if (sec[510] == 0x55 && sec[511] == 0xAA && part_num <= 4) {
            uint8_t type = sec[446 + (part_num - 1) * 16 + 4];
            if (type == 0xEF) {
                esp = true;
            }
        }
    }

    close(pfd);
    return esp;
}

static int compare_disks(const void *a, const void *b) {
    const disk_info_t *da = (const disk_info_t *)a;
    const disk_info_t *db = (const disk_info_t *)b;
    return strcmp(da->devname, db->devname);
}

static int load_disks(disk_info_t *items, int max) {
    int count = 0;
    DIR *dir = opendir("/dev");
    if (!dir) return 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max) {
        const char *name = ent->d_name;
        if (strncmp(name, "sd", 2) != 0 && strncmp(name, "hd", 2) != 0 && strncmp(name, "vd", 2) != 0)
            continue;

        char path[128];
        snprintf(path, sizeof(path), "/dev/%.60s", name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        off_t size_bytes = lseek(fd, 0, SEEK_END);
        if (size_bytes < 0) {
            close(fd);
            continue;
        }

        memset(&items[count], 0, sizeof(disk_info_t));
        snprintf(items[count].devname, sizeof(items[count].devname), "%.15s", name);
        items[count].total_sectors = (uint32_t)(size_bytes / 512);

        size_t len = strlen(name);
        if (len > 0 && name[len - 1] >= '0' && name[len - 1] <= '9') {
            items[count].is_partition = true;

            uint8_t sec0[512];
            lseek(fd, 0, SEEK_SET);
            if (read(fd, sec0, 512) == 512) {
                if (sec0[510] == 0x55 && sec0[511] == 0xAA && memcmp(sec0 + 82, "FAT32   ", 8) == 0) {
                    items[count].is_fat32 = true;
                    char lbl[12];
                    memcpy(lbl, sec0 + 71, 11);
                    lbl[11] = '\0';
                    int k = 10;
                    while (k >= 0 && (lbl[k] == ' ' || lbl[k] == '\0')) {
                        lbl[k--] = '\0';
                    }
                    if (lbl[0] && strcmp(lbl, "NO NAME") != 0) {
                        snprintf(items[count].label, sizeof(items[count].label), "%s", lbl);
                    }
                }
            }

            if (!items[count].label[0]) {
                uint8_t sb[512];
                lseek(fd, 1024, SEEK_SET);
                if (read(fd, sb, 512) == 512) {
                    uint16_t magic = *(uint16_t*)(sb + 0x38);
                    if (magic == 0xEF53) {
                        char vol_name[17];
                        memcpy(vol_name, sb + 0x78, 16);
                        vol_name[16] = '\0';
                        if (vol_name[0]) {
                            snprintf(items[count].label, sizeof(items[count].label), "%s", vol_name);
                        }
                    }
                }
            }

            char parent[16];
            snprintf(parent, sizeof(parent), "%s", name);
            size_t plen = strlen(parent);
            while (plen > 0 && parent[plen - 1] >= '0' && parent[plen - 1] <= '9') {
                plen--;
            }
            int part_num = atoi(parent + plen);
            parent[plen] = '\0';
            if (part_num > 0 && plen > 0) {
                items[count].is_esp = disk_partition_is_esp(parent, part_num);
            }
        } else {
            items[count].is_partition = false;
        }

        close(fd);
        count++;
    }
    closedir(dir);

    if (count > 1) {
        qsort(items, count, sizeof(disk_info_t), compare_disks);
    }
    return count;
}

static void usage(void) {
    printf("Usage: lsblk [-r] [--json] [/dev/DEVICE]\n");
}

int main(int argc, char **argv) {
    disk_info_t items[LSBLK_MAX_DISKS];
    const char *filter = NULL;
    int raw = 0;
    int json = 0;
    int count;
    int printed = 0;

    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-r")) {
            raw = 1;
        } else if (streq(argv[i], "--json")) {
            json = 1;
        } else if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            printf("lsblk: unknown option: %s\n", argv[i]);
            usage();
            return 1;
        } else if (!filter) {
            filter = device_name_arg(argv[i]);
        } else {
            printf("lsblk: only one device filter is supported\n");
            return 1;
        }
    }

    if (raw && json) {
        printf("lsblk: -r and --json cannot be used together\n");
        return 1;
    }

    count = load_disks(items, LSBLK_MAX_DISKS);

    if (json) {
        printf("{\"devices\":[");

        for (int i = 0; i < count; i++) {
            if (items[i].is_partition) continue;
            if (filter && !streq(items[i].devname, filter)) continue;
            if (printed > 0) printf(",");
            print_json_disk(&items[i], items, count);
            printed++;
        }

        if (filter && printed == 0) {
            for (int i = 0; i < count; i++) {
                if (!items[i].is_partition || !streq(items[i].devname, filter)) continue;
                print_json_partition(&items[i]);
                printed++;
                break;
            }
        }

        printf("]}\n");
    } else if (raw) {
        for (int i = 0; i < count; i++) {
            if (items[i].is_partition) continue;
            if (filter && !streq(items[i].devname, filter)) continue;
            print_raw_disk(&items[i], items, count);
            printed++;
        }

        if (filter && printed == 0) {
            for (int i = 0; i < count; i++) {
                if (!items[i].is_partition || !streq(items[i].devname, filter)) continue;
                print_raw_device(&items[i]);
                printed++;
                break;
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (items[i].is_partition) continue;
            if (filter && !streq(items[i].devname, filter)) continue;
            print_tree_disk(&items[i], items, count);
            printed++;
        }

        if (filter && printed == 0) {
            for (int i = 0; i < count; i++) {
                if (!items[i].is_partition || !streq(items[i].devname, filter)) continue;
                print_tree_device(&items[i], "");
                printed++;
                break;
            }
        }
    }

    if (printed == 0 && !json) {
        if (filter) printf("lsblk: /dev/%s not found\n", filter);
        else printf("lsblk: no block devices found\n");
        return 1;
    }

    if (printed == 0 && filter) return 1;
    return 0;
}
