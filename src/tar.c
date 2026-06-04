// Copyright (c) 2026 Lluciocc (https://github.com/lluciocc)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <syscall.h> // to fix Fat32_FileInfo definition
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdint.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#define TAR_BLOCK_SIZE 512
#define LZ4_BLOCK_MAX_SIZE (64 * 1024)
#define XXH_PRIME32_1 2654435761U
#define XXH_PRIME32_2 2246822519U
#define XXH_PRIME32_3 3266489917U
#define XXH_PRIME32_4  668265263U
#define XXH_PRIME32_5  374761393U

static inline uint32_t rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

static uint32_t xxh32_small(const uint8_t* p, size_t len, uint32_t seed) {
    const uint8_t* const end = p + len;
    uint32_t h32 = seed + XXH_PRIME32_5;
    h32 += (uint32_t)len;

    while (p + 4 <= end) {
        uint32_t val = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
        h32 += val * XXH_PRIME32_3;
        h32 = rotl32(h32, 17) * XXH_PRIME32_4;
        p += 4;
    }
    while (p < end) {
        h32 += (*p) * XXH_PRIME32_5;
        h32 = rotl32(h32, 11) * XXH_PRIME32_1;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= XXH_PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= XXH_PRIME32_3;
    h32 ^= h32 >> 16;
    return h32;
}

static int lz4_decompress_block(const uint8_t *src, int src_len, uint8_t *dst, int dst_len) {
    const uint8_t *ip = src;
    const uint8_t *ip_limit = src + src_len;
    uint8_t *op = dst;
    uint8_t *op_limit = dst + dst_len;

    while (ip < ip_limit) {
        uint8_t token = *ip++;
        int literal_len = token >> 4;

        if (literal_len == 15) {
            uint8_t s;
            do {
                if (ip >= ip_limit) return -1;
                s = *ip++;
                literal_len += s;
            } while (s == 255);
        }

        if (literal_len > 0) {
            if (op + literal_len > op_limit || ip + literal_len > ip_limit) {
                return -2;
            }
            memcpy(op, ip, literal_len);
            op += literal_len;
            ip += literal_len;
        }

        if (ip >= ip_limit) {
            break;
        }

        if (ip + 2 > ip_limit) return -3;
        uint16_t offset = ip[0] | (ip[1] << 8);
        ip += 2;

        if (offset == 0) {
            return -4;
        }

        int match_len = token & 0x0F;
        if (match_len == 15) {
            uint8_t s;
            do {
                if (ip >= ip_limit) return -5;
                s = *ip++;
                match_len += s;
            } while (s == 255);
        }
        match_len += 4;

        uint8_t *ref = op - offset;
        if (ref < dst || op + match_len > op_limit) {
            return -6;
        }
        
        for (int i = 0; i < match_len; i++) {
            *op++ = *ref++;
        }
    }
    return (int)(op - dst);
}

static inline uint32_t lz4_hash(uint32_t val) {
    return (val * 2654435761U) >> (32 - 12);
}

static int lz4_compress_block(const uint8_t* src, int src_len, uint8_t* dst, int dst_len) {
    const uint8_t* ip = src;
    const uint8_t* anchor = ip;
    const uint8_t* const mflimit = src + src_len - 12;
    uint8_t* op = dst;
    uint8_t* const op_limit = dst + dst_len;

    const uint8_t* hash_table[4096] = {0};

    while (ip < mflimit) {
        uint32_t val = ip[0] | (ip[1] << 8) | (ip[2] << 16) | (ip[3] << 24);
        uint32_t h = lz4_hash(val);
        const uint8_t* ref = hash_table[h];
        hash_table[h] = ip;

        if (ref >= src && ip - ref < 65536 &&
            ref[0] == ip[0] && ref[1] == ip[1] && ref[2] == ip[2] && ref[3] == ip[3]) {
            
            int lit_len = (int)(ip - anchor);
            int match_len = 4;
            while (ip + match_len < src + src_len && ref[match_len] == ip[match_len]) {
                match_len++;
            }
            
            int est_op_len = 1 + (lit_len + 254) / 255 + lit_len + 2 + (match_len + 254) / 255;
            if (op + est_op_len > op_limit) {
                return 0;
            }

            int lit_token = lit_len >= 15 ? 15 : lit_len;
            int match_token = (match_len - 4) >= 15 ? 15 : (match_len - 4);
            *op++ = (lit_token << 4) | match_token;

            if (lit_len >= 15) {
                int len = lit_len - 15;
                while (len >= 255) {
                    *op++ = 255;
                    len -= 255;
                }
                *op++ = len;
            }

            memcpy(op, anchor, lit_len);
            op += lit_len;

            uint16_t offset = (uint16_t)(ip - ref);
            *op++ = offset & 0xFF;
            *op++ = (offset >> 8) & 0xFF;

            if (match_len - 4 >= 15) {
                int len = match_len - 4 - 15;
                while (len >= 255) {
                    *op++ = 255;
                    len -= 255;
                }
                *op++ = len;
            }

            ip += match_len;
            anchor = ip;
        } else {
            ip++;
        }
    }

    int lit_len = (int)((src + src_len) - anchor);
    int est_op_len = 1 + (lit_len + 254) / 255 + lit_len;
    if (op + est_op_len > op_limit) {
        return 0;
    }

    int lit_token = lit_len >= 15 ? 15 : lit_len;
    *op++ = (lit_token << 4);
    if (lit_len >= 15) {
        int len = lit_len - 15;
        while (len >= 255) {
            *op++ = 255;
            len -= 255;
        }
        *op++ = len;
    }
    memcpy(op, anchor, lit_len);
    op += lit_len;

    return (int)(op - dst);
}

// --- tar_file_t abstraction ---
typedef struct {
    int fd;
    int is_lz4;
    int is_writing;
    int block_checksum;
    
    // For LZ4 compression
    uint8_t *uncomp_buf;
    int uncomp_len;
    
    // For LZ4 decompression
    uint8_t *decomp_buf;
    int decomp_len;
    int decomp_pos;
    int decomp_max_size;
    
    // Buffer for compressed block data
    uint8_t *comp_buf;
} tar_file_t;

static int tar_flush_block(tar_file_t *f) {
    if (f->uncomp_len == 0) return 0;
    
    int comp_size = lz4_compress_block(f->uncomp_buf, f->uncomp_len, f->comp_buf, LZ4_BLOCK_MAX_SIZE);
    
    if (comp_size > 0 && comp_size < f->uncomp_len) {
        uint32_t block_header = comp_size;
        if (sys_write_fs(f->fd, &block_header, 4) != 4) return -1;
        
        int written = 0;
        while (written < comp_size) {
            int w = sys_write_fs(f->fd, f->comp_buf + written, comp_size - written);
            if (w <= 0) return -1;
            written += w;
        }
    } else {
        uint32_t block_header = f->uncomp_len | 0x80000000;
        if (sys_write_fs(f->fd, &block_header, 4) != 4) return -1;
        
        int written = 0;
        while (written < f->uncomp_len) {
            int w = sys_write_fs(f->fd, f->uncomp_buf + written, f->uncomp_len - written);
            if (w <= 0) return -1;
            written += w;
        }
    }
    
    f->uncomp_len = 0;
    return 0;
}

static tar_file_t* tar_open(const char *path, const char *mode, int force_lz4) {
    int fd = -1;
    int is_writing = 0;
    if (strcmp(mode, "w") == 0) {
        fd = sys_open(path, "w");
        is_writing = 1;
    } else if (strcmp(mode, "r") == 0) {
        fd = sys_open(path, "r");
        is_writing = 0;
    }
    
    if (fd < 0) return NULL;
    
    tar_file_t *f = (tar_file_t *)malloc(sizeof(tar_file_t));
    if (!f) {
        sys_close(fd);
        return NULL;
    }
    
    f->fd = fd;
    f->is_writing = is_writing;
    f->is_lz4 = force_lz4;
    f->block_checksum = 0;
    f->uncomp_buf = NULL;
    f->uncomp_len = 0;
    f->decomp_buf = NULL;
    f->decomp_len = 0;
    f->decomp_pos = 0;
    f->decomp_max_size = 0;
    f->comp_buf = NULL;
    
    if (is_writing) {
        int path_len = strlen(path);
        if (force_lz4 || (path_len >= 4 && strcmp(path + path_len - 4, ".lz4") == 0)) {
            f->is_lz4 = 1;
        }
        
        if (f->is_lz4) {
            f->uncomp_buf = (uint8_t *)malloc(LZ4_BLOCK_MAX_SIZE);
            f->comp_buf = (uint8_t *)malloc(LZ4_BLOCK_MAX_SIZE + 4096);
            if (!f->uncomp_buf || !f->comp_buf) {
                if (f->uncomp_buf) free(f->uncomp_buf);
                if (f->comp_buf) free(f->comp_buf);
                sys_close(fd);
                free(f);
                return NULL;
            }
            
            uint32_t magic = 0x184D2204;
            sys_write_fs(fd, &magic, 4);
            
            uint8_t desc[2] = {0x60, 0x40};
            sys_write_fs(fd, desc, 2);
            
            uint32_t hash = xxh32_small(desc, 2, 0);
            uint8_t hc = (hash >> 8) & 0xFF;
            sys_write_fs(fd, &hc, 1);
        }
    } else {
        uint32_t magic = 0;
        int got = sys_read(fd, &magic, 4);
        if (got == 4 && magic == 0x184D2204) {
            f->is_lz4 = 1;
            
            uint8_t flg = 0;
            uint8_t bd = 0;
            sys_read(fd, &flg, 1);
            sys_read(fd, &bd, 1);
            
            f->block_checksum = (flg >> 4) & 1;
            int content_size_flag = (flg >> 3) & 1;
            int dict_id_flag = flg & 1;
            
            if (content_size_flag) {
                uint64_t dummy;
                sys_read(fd, &dummy, 8);
            }
            if (dict_id_flag) {
                uint32_t dummy;
                sys_read(fd, &dummy, 4);
            }
            
            uint8_t hc;
            sys_read(fd, &hc, 1);
            
            int block_size_id = (bd >> 4) & 7;
            int max_block_size = 4 * 1024 * 1024;
            if (block_size_id == 4) max_block_size = 64 * 1024;
            else if (block_size_id == 5) max_block_size = 256 * 1024;
            else if (block_size_id == 6) max_block_size = 1024 * 1024;
            else if (block_size_id == 7) max_block_size = 4 * 1024 * 1024;
            
            f->decomp_max_size = max_block_size;
            f->decomp_buf = (uint8_t *)malloc(max_block_size);
            f->comp_buf = (uint8_t *)malloc(max_block_size + 4096);
            if (!f->decomp_buf || !f->comp_buf) {
                if (f->decomp_buf) free(f->decomp_buf);
                if (f->comp_buf) free(f->comp_buf);
                sys_close(fd);
                free(f);
                return NULL;
            }
        } else {
            sys_close(fd);
            fd = sys_open(path, "r");
            if (fd < 0) {
                free(f);
                return NULL;
            }
            f->fd = fd;
            f->is_lz4 = 0;
        }
    }
    
    return f;
}

static int tar_read(tar_file_t *f, void *buf, int len) {
    if (!f->is_lz4) {
        return sys_read(f->fd, buf, len);
    }
    
    uint8_t *dst = (uint8_t *)buf;
    int done = 0;
    
    while (done < len) {
        if (f->decomp_pos < f->decomp_len) {
            int avail = f->decomp_len - f->decomp_pos;
            int chunk = (len - done) < avail ? (len - done) : avail;
            memcpy(dst + done, f->decomp_buf + f->decomp_pos, chunk);
            f->decomp_pos += chunk;
            done += chunk;
        } else {
            uint32_t block_size = 0;
            uint8_t *bs_ptr = (uint8_t *)&block_size;
            int bs_read = 0;
            while (bs_read < 4) {
                int r = sys_read(f->fd, bs_ptr + bs_read, 4 - bs_read);
                if (r <= 0) {
                    if (bs_read == 0 && r == 0) {
                        block_size = 0;
                        break;
                    }
                    return -1;
                }
                bs_read += r;
            }
            
            if (block_size == 0) {
                break;
            }
            
            int is_uncompressed = (block_size & 0x80000000) != 0;
            uint32_t real_size = block_size & 0x7FFFFFFF;
            
            if (real_size > (uint32_t)f->decomp_max_size + 4096) {
                return -1;
            }
            
            uint32_t bytes_read = 0;
            while (bytes_read < real_size) {
                int r = sys_read(f->fd, f->comp_buf + bytes_read, real_size - bytes_read);
                if (r <= 0) {
                    return -1;
                }
                bytes_read += r;
            }
            
            if (f->block_checksum) {
                uint32_t dummy_checksum;
                if (sys_read(f->fd, &dummy_checksum, 4) != 4) {
                    return -1;
                }
            }
            
            if (is_uncompressed) {
                memcpy(f->decomp_buf, f->comp_buf, real_size);
                f->decomp_len = real_size;
            } else {
                int decomp_bytes = lz4_decompress_block(f->comp_buf, real_size, f->decomp_buf, f->decomp_max_size);
                if (decomp_bytes < 0) {
                    return -1;
                }
                f->decomp_len = decomp_bytes;
            }
            f->decomp_pos = 0;
        }
    }
    
    return done;
}

static int tar_write(tar_file_t *f, const void *buf, int len) {
    if (!f->is_lz4) {
        return sys_write_fs(f->fd, buf, len);
    }
    
    const uint8_t *src = (const uint8_t *)buf;
    int done = 0;
    
    while (done < len) {
        int avail = LZ4_BLOCK_MAX_SIZE - f->uncomp_len;
        int chunk = (len - done) < avail ? (len - done) : avail;
        memcpy(f->uncomp_buf + f->uncomp_len, src + done, chunk);
        f->uncomp_len += chunk;
        done += chunk;
        
        if (f->uncomp_len == LZ4_BLOCK_MAX_SIZE) {
            if (tar_flush_block(f) != 0) {
                return -1;
            }
        }
    }
    
    return done;
}

static int tar_close(tar_file_t *f) {
    int ret = 0;
    if (f->is_writing) {
        if (f->is_lz4) {
            if (tar_flush_block(f) != 0) {
                ret = -1;
            }
            uint32_t end_mark = 0;
            if (sys_write_fs(f->fd, &end_mark, 4) != 4) {
                ret = -1;
            }
        }
    }
    
    sys_close(f->fd);
    
    if (f->uncomp_buf) free(f->uncomp_buf);
    if (f->decomp_buf) free(f->decomp_buf);
    if (f->comp_buf) free(f->comp_buf);
    free(f);
    
    return ret;
}


#define TAR_BLOCK_SIZE 512
#define TAR_BUFFER_SIZE 4096
#define TAR_PATH_MAX 1024
#define TAR_LIST_ENTRIES 256

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed));

typedef char tar_header_size_must_be_512[(sizeof(struct tar_header) == TAR_BLOCK_SIZE) ? 1 : -1];

static void print_usage(void) {
    printf("Usage:\n");
    printf("  tar -cf archive.tar path...\n");
    printf("  tar -xf archive.tar\n");
    printf("  tar -tf archive.tar\n");
    printf("  tar -czf archive.tar.lz4 path...\n");
    printf("  tar -xzf archive.tar.lz4\n");
    printf("  tar -tzf archive.tar.lz4\n");
    printf("Options:\n");
    printf("  -q, --quiet, --silent  Suppress warnings\n");
}

static int is_zero_block(const unsigned char *block) {
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) return 0;
    }
    return 1;
}

static int safe_copy(char *dst, int dst_size, const char *src) {
    int i = 0;

    if (dst_size <= 0) return -1;
    while (src[i]) {
        if (i >= dst_size - 1) return -1;
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return 0;
}

static int join_path(char *out, int out_size, const char *left, const char *right) {
    int len = 0;

    if (out_size <= 0) return -1;

    while (left[len]) {
        if (len >= out_size - 1) return -1;
        out[len] = left[len];
        len++;
    }

    if (len > 0 && out[len - 1] != '/') {
        if (len >= out_size - 1) return -1;
        out[len++] = '/';
    }

    for (int i = 0; right[i]; i++) {
        if (len >= out_size - 1) return -1;
        out[len++] = right[i];
    }

    out[len] = '\0';
    return 0;
}

static void strip_leading_slashes(const char **path) {
    while (**path == '/') {
        (*path)++;
    }
}

static int strip_trailing_slashes_copy(char *out, int out_size, const char *path) {
    int len;

    if (safe_copy(out, out_size, path) != 0) return -1;
    len = (int)strlen(out);
    while (len > 1 && out[len - 1] == '/') {
        out[--len] = '\0';
    }
    return 0;
}

static int has_unsafe_component(const char *path) {
    int start = 0;

    if (path[0] == '/') return 1;

    while (path[start]) {
        int end = start;
        while (path[end] && path[end] != '/') end++;

        if ((end - start) == 2 && path[start] == '.' && path[start + 1] == '.') {
            return 1;
        }

        start = end;
        while (path[start] == '/') start++;
    }

    return 0;
}

static int make_absolute_path(char *out, int out_size, const char *path) {
    char cwd[TAR_PATH_MAX];

    if (path[0] == '/') {
        return safe_copy(out, out_size, path);
    }

    if (sys_getcwd(cwd, sizeof(cwd)) < 0) {
        return -1;
    }

    return join_path(out, out_size, cwd, path);
}

static int make_directory_recursive(const char *path) {
    char tmp[TAR_PATH_MAX];
    int len = 0;

    if (!path || path[0] == '\0') return 0;
    if (make_absolute_path(tmp, sizeof(tmp), path) != 0) return -1;

    len = (int)strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }

    for (int i = 1; tmp[i]; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0] != '\0' && !sys_exists(tmp)) {
                if (sys_mkdir(tmp) != 0 && !sys_exists(tmp)) return -1;
            }
            tmp[i] = '/';
        }
    }

    if (!sys_exists(tmp)) {
        if (sys_mkdir(tmp) != 0 && !sys_exists(tmp)) return -1;
    }

    return 0;
}

static int make_parent_directories(const char *path) {
    char parent[TAR_PATH_MAX];
    int last_slash = -1;

    if (safe_copy(parent, sizeof(parent), path) != 0) return -1;
    for (int i = 0; parent[i]; i++) {
        if (parent[i] == '/') last_slash = i;
    }

    if (last_slash <= 0) return 0;
    parent[last_slash] = '\0';
    return make_directory_recursive(parent);
}

static uint64_t octal_to_uint(const char *field, int size) {
    uint64_t value = 0;

    for (int i = 0; i < size; i++) {
        char c = field[i];
        if (c == '\0' || c == ' ') break;
        if (c < '0' || c > '7') continue;
        value = (value << 3) + (uint64_t)(c - '0');
    }

    return value;
}

static void uint_to_octal(uint64_t value, char *field, int size) {
    memset(field, '0', size);
    field[size - 1] = '\0';

    for (int i = size - 2; i >= 0; i--) {
        field[i] = (char)('0' + (value & 7));
        value >>= 3;
    }
}

static uint32_t calculate_checksum(struct tar_header *header) {
    unsigned char *bytes = (unsigned char *)header;
    uint32_t sum = 0;

    memset(header->checksum, ' ', sizeof(header->checksum));
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        sum += bytes[i];
    }
    return sum;
}

static void write_checksum(struct tar_header *header, uint32_t checksum) {
    for (int i = 0; i < 6; i++) {
        header->checksum[5 - i] = (char)('0' + (checksum & 7));
        checksum >>= 3;
    }
    header->checksum[6] = '\0';
    header->checksum[7] = ' ';
}

static int write_all_raw(int fd, const void *buf, uint32_t len) {
    const char *p = (const char *)buf;
    uint32_t done = 0;

    while (done < len) {
        int written = sys_write_fs(fd, p + done, len - done);
        if (written <= 0) return -1;
        done += (uint32_t)written;
    }

    return 0;
}

static int write_all(tar_file_t *fd, const void *buf, uint32_t len) {
    const char *p = (const char *)buf;
    uint32_t done = 0;

    while (done < len) {
        int written = tar_write(fd, p + done, len - done);
        if (written <= 0) return -1;
        done += (uint32_t)written;
    }

    return 0;
}

static int read_exact(tar_file_t *fd, void *buf, uint32_t len) {
    char *p = (char *)buf;
    uint32_t done = 0;

    while (done < len) {
        int got = tar_read(fd, p + done, len - done);
        if (got <= 0) return -1;
        done += (uint32_t)got;
    }

    return 0;
}

static int write_padding(tar_file_t *fd, uint64_t size) {
    static const char zeros[TAR_BLOCK_SIZE] = {0};
    uint32_t pad = (uint32_t)((TAR_BLOCK_SIZE - (size % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE);

    if (pad == 0) return 0;
    return write_all(fd, zeros, pad);
}

static int split_ustar_path(const char *path, char *name, char *prefix) {
    int len = (int)strlen(path);

    memset(name, 0, 100);
    memset(prefix, 0, 155);

    if (len <= 100) {
        memcpy(name, path, len);
        return 0;
    }

    for (int i = len - 1; i > 0; i--) {
        int prefix_len;
        int name_len;

        if (path[i] != '/') continue;

        prefix_len = i;
        name_len = len - i - 1;
        if (prefix_len <= 155 && name_len > 0 && name_len <= 100) {
            memcpy(prefix, path, prefix_len);
            memcpy(name, path + i + 1, name_len);
            return 0;
        }
    }

    return -1;
}

static int write_header(tar_file_t *archive_fd, const char *archive_path, uint64_t size, char typeflag) {
    struct tar_header header;
    uint32_t checksum;

    memset(&header, 0, sizeof(header));

    if (split_ustar_path(archive_path, header.name, header.prefix) != 0) {
        printf("tar: path too long for ustar: %s\n", archive_path);
        return -1;
    }

    uint_to_octal(typeflag == '5' ? 0755 : 0644, header.mode, sizeof(header.mode));
    uint_to_octal(0, header.uid, sizeof(header.uid));
    uint_to_octal(0, header.gid, sizeof(header.gid));
    uint_to_octal(typeflag == '5' ? 0 : size, header.size, sizeof(header.size));
    uint_to_octal(0, header.mtime, sizeof(header.mtime));
    header.typeflag = typeflag;
    memcpy(header.magic, "ustar", 5);
    memcpy(header.version, "00", 2);

    checksum = calculate_checksum(&header);
    write_checksum(&header, checksum);

    return write_all(archive_fd, &header, sizeof(header));
}

static int make_archive_path(char *out, int out_size, const char *input_path, int is_directory) {
    char tmp[TAR_PATH_MAX];
    const char *p = input_path;
    int len;

    strip_leading_slashes(&p);
    if (*p == '\0') {
        printf("tar: refusing to archive root path '%s'\n", input_path);
        return -1;
    }

    if (safe_copy(tmp, sizeof(tmp), p) != 0) return -1;
    len = (int)strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }

    if (is_directory) {
        if (len + 1 >= out_size) return -1;
        memcpy(out, tmp, len);
        out[len++] = '/';
        out[len] = '\0';
    } else {
        if (safe_copy(out, out_size, tmp) != 0) return -1;
    }

    return 0;
}

static int add_file_to_tar(tar_file_t *archive_fd, const char *fs_path, const char *archive_path) {
    FAT32_FileInfo info;
    int input_fd;
    char buffer[TAR_BUFFER_SIZE];
    uint64_t remaining;

    if (sys_get_file_info(fs_path, &info) != 0 || info.is_directory) {
        printf("tar: cannot stat file '%s'\n", fs_path);
        return -1;
    }

    input_fd = sys_open(fs_path, "r");
    if (input_fd < 0) {
        printf("tar: cannot open '%s'\n", fs_path);
        return -1;
    }

    if (write_header(archive_fd, archive_path, info.size, '0') != 0) {
        sys_close(input_fd);
        return -1;
    }

    remaining = info.size;
    while (remaining > 0) {
        uint32_t chunk = remaining > TAR_BUFFER_SIZE ? TAR_BUFFER_SIZE : (uint32_t)remaining;
        int got = sys_read(input_fd, buffer, chunk);
        if (got <= 0) {
            printf("tar: read error on '%s'\n", fs_path);
            sys_close(input_fd);
            return -1;
        }
        if (write_all(archive_fd, buffer, (uint32_t)got) != 0) {
            printf("tar: write error on archive while adding '%s'\n", fs_path);
            sys_close(input_fd);
            return -1;
        }
        remaining -= (uint32_t)got;
    }

    sys_close(input_fd);

    /* File data is padded so the next header begins on a 512-byte boundary. */
    if (write_padding(archive_fd, info.size) != 0) {
        printf("tar: write error on archive padding\n");
        return -1;
    }

    return 0;
}

static int add_directory_recursive(tar_file_t *archive_fd, const char *fs_path, const char *archive_path) {
    FAT32_FileInfo *entries;
    int count;
    int had_error = 0;

    if (write_header(archive_fd, archive_path, 0, '5') != 0) return -1;

    entries = (FAT32_FileInfo *)malloc(sizeof(FAT32_FileInfo) * TAR_LIST_ENTRIES);
    if (!entries) {
        printf("tar: out of memory reading directory '%s'\n", fs_path);
        return -1;
    }

    count = sys_list(fs_path, entries, TAR_LIST_ENTRIES);
    if (count < 0) {
        printf("tar: cannot read directory '%s'\n", fs_path);
        free(entries);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        char child_fs[TAR_PATH_MAX];
        char child_archive[TAR_PATH_MAX];
        int child_len;

        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) {
            continue;
        }

        if (join_path(child_fs, sizeof(child_fs), fs_path, entries[i].name) != 0) {
            printf("tar: path too long below '%s'\n", fs_path);
            had_error = 1;
            continue;
        }

        if (join_path(child_archive, sizeof(child_archive), archive_path, entries[i].name) != 0) {
            printf("tar: archive path too long below '%s'\n", archive_path);
            had_error = 1;
            continue;
        }

        child_len = (int)strlen(child_archive);
        if (entries[i].is_directory) {
            if (child_len + 1 >= (int)sizeof(child_archive)) {
                printf("tar: archive path too long below '%s'\n", archive_path);
                had_error = 1;
                continue;
            }
            child_archive[child_len++] = '/';
            child_archive[child_len] = '\0';
            if (add_directory_recursive(archive_fd, child_fs, child_archive) != 0) had_error = 1;
        } else {
            if (add_file_to_tar(archive_fd, child_fs, child_archive) != 0) had_error = 1;
        }
    }

    free(entries);
    return had_error ? -1 : 0;
}

static int add_path_to_tar(tar_file_t *archive_fd, const char *path) {
    FAT32_FileInfo info;
    char fs_path[TAR_PATH_MAX];
    char archive_path[TAR_PATH_MAX];

    if (strip_trailing_slashes_copy(fs_path, sizeof(fs_path), path) != 0) {
        printf("tar: path too long: %s\n", path);
        return -1;
    }

    if (sys_get_file_info(fs_path, &info) != 0) {
        printf("tar: cannot stat '%s'\n", path);
        return -1;
    }

    if (make_archive_path(archive_path, sizeof(archive_path), fs_path, info.is_directory) != 0) {
        printf("tar: cannot store path '%s'\n", path);
        return -1;
    }

    if (info.is_directory) {
        return add_directory_recursive(archive_fd, fs_path, archive_path);
    }

    return add_file_to_tar(archive_fd, fs_path, archive_path);
}

static int build_full_name(const struct tar_header *header, char *out, int out_size) {
    int pos = 0;

    if (out_size <= 0) return -1;

    if (header->prefix[0]) {
        for (int i = 0; i < 155 && header->prefix[i]; i++) {
            if (pos >= out_size - 1) return -1;
            out[pos++] = header->prefix[i];
        }
        if (pos >= out_size - 1) return -1;
        out[pos++] = '/';
    }

    for (int i = 0; i < 100 && header->name[i]; i++) {
        if (pos >= out_size - 1) return -1;
        out[pos++] = header->name[i];
    }

    out[pos] = '\0';
    return pos > 0 ? 0 : -1;
}

static int valid_ustar_magic(const struct tar_header *header) {
    return memcmp(header->magic, "ustar", 5) == 0;
}

static int skip_bytes(tar_file_t *fd, uint64_t size) {
    char buffer[TAR_BUFFER_SIZE];
    uint64_t remaining = size;

    while (remaining > 0) {
        uint32_t chunk = remaining > TAR_BUFFER_SIZE ? TAR_BUFFER_SIZE : (uint32_t)remaining;
        int got = tar_read(fd, buffer, chunk);
        if (got <= 0) return -1;
        remaining -= (uint32_t)got;
    }

    return 0;
}

static int skip_entry_data(tar_file_t *fd, uint64_t size) {
    uint64_t total = size + ((TAR_BLOCK_SIZE - (size % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE);
    return skip_bytes(fd, total);
}

static int extract_entry(tar_file_t *archive_fd, const struct tar_header *header, const char *dest_path, const char *archive_path, uint64_t size, int quiet) {
    char buffer[TAR_BUFFER_SIZE];
    uint64_t remaining;
    int out_fd;

    if (has_unsafe_component(archive_path)) {
        if (!quiet) printf("tar: skipping unsafe path '%s'\n", archive_path);
        return skip_entry_data(archive_fd, size);
    }

    if (header->typeflag == '5') {
        if (make_directory_recursive(dest_path) != 0) {
            if (!quiet) printf("tar: cannot create directory '%s'\n", dest_path);
            skip_entry_data(archive_fd, size);
            return -1;
        }
        return skip_entry_data(archive_fd, size);
    }

    if (header->typeflag != '0' && header->typeflag != '\0') {
        if (header->typeflag != 'x' && header->typeflag != 'g' && strstr(archive_path, "PaxHeader") == NULL) {
            if (!quiet) printf("tar: skipping unsupported entry '%s'\n", archive_path);
        }
        return skip_entry_data(archive_fd, size);
    }

    if (make_parent_directories(dest_path) != 0) {
        if (!quiet) printf("tar: cannot create parent directories for '%s'\n", dest_path);
        return skip_entry_data(archive_fd, size);
    }

    out_fd = sys_open(dest_path, "w");
    if (out_fd < 0) {
        if (!quiet) printf("tar: cannot create '%s'\n", dest_path);
        return skip_entry_data(archive_fd, size);
    }

    remaining = size;
    while (remaining > 0) {
        uint32_t chunk = remaining > TAR_BUFFER_SIZE ? TAR_BUFFER_SIZE : (uint32_t)remaining;
        int got = tar_read(archive_fd, buffer, chunk);
        if (got <= 0) {
            if (!quiet) printf("tar: unexpected end of archive while reading '%s'\n", archive_path);
            sys_close(out_fd);
            return -1;
        }
        if (write_all_raw(out_fd, buffer, (uint32_t)got) != 0) {
            if (!quiet) printf("tar: write error on '%s'\n", archive_path);
            sys_close(out_fd);
            skip_entry_data(archive_fd, remaining - (uint32_t)got);
            return -1;
        }
        remaining -= (uint32_t)got;
    }

    sys_close(out_fd);
    return skip_bytes(archive_fd, (TAR_BLOCK_SIZE - (size % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE);
}

static int create_archive(const char *archive_name, int path_count, char **paths, int force_lz4) {
    static const char zero_block[TAR_BLOCK_SIZE] = {0};
    tar_file_t *archive_fd;
    int had_error = 0;

    archive_fd = tar_open(archive_name, "w", force_lz4);
    if (!archive_fd) {
        printf("tar: cannot create '%s'\n", archive_name);
        return 1;
    }

    for (int i = 0; i < path_count; i++) {
        if (add_path_to_tar(archive_fd, paths[i]) != 0) had_error = 1;
    }

    /* Two zero blocks mark end-of-archive for standard tar readers. */
    if (write_all(archive_fd, zero_block, TAR_BLOCK_SIZE) != 0 ||
        write_all(archive_fd, zero_block, TAR_BLOCK_SIZE) != 0) {
        printf("tar: write error on '%s'\n", archive_name);
        had_error = 1;
    }

    tar_close(archive_fd);
    return had_error ? 1 : 0;
}

static int read_archive(const char *archive_name, int extract, int force_lz4, const char *target_dir, int quiet) {
    tar_file_t *archive_fd;
    int had_error = 0;

    archive_fd = tar_open(archive_name, "r", force_lz4);
    if (!archive_fd) {
        if (!quiet) printf("tar: cannot open '%s'\n", archive_name);
        return 1;
    }

    while (1) {
        struct tar_header header;
        char path[TAR_PATH_MAX];
        uint64_t size;

        if (read_exact(archive_fd, &header, TAR_BLOCK_SIZE) != 0) {
            if (!quiet) printf("tar: unexpected end of archive\n");
            had_error = 1;
            break;
        }

        if (is_zero_block((const unsigned char *)&header)) {
            break;
        }

        if (!valid_ustar_magic(&header)) {
            if (!quiet) printf("tar: invalid or unsupported archive format\n");
            had_error = 1;
            break;
        }

        if (build_full_name(&header, path, sizeof(path)) != 0) {
            if (!quiet) printf("tar: entry path too long\n");
            had_error = 1;
            break;
        }

        size = octal_to_uint(header.size, sizeof(header.size));

        if (extract) {
            char dest_path[TAR_PATH_MAX];
            if (target_dir) {
                if (join_path(dest_path, sizeof(dest_path), target_dir, path) != 0) {
                    if (!quiet) printf("tar: target path too long\n");
                    had_error = 1;
                    break;
                }
            } else {
                strcpy(dest_path, path);
            }
            if (extract_entry(archive_fd, &header, dest_path, path, size, quiet) != 0) had_error = 1;
        } else {
            if (header.typeflag != 'x' && header.typeflag != 'g' && strstr(path, "PaxHeader") == NULL) {
                printf("%s\n", path);
            }
            if (skip_entry_data(archive_fd, size) != 0) {
                if (!quiet) printf("tar: unexpected end of archive\n");
                had_error = 1;
                break;
            }
        }
    }

    tar_close(archive_fd);
    return had_error ? 1 : 0;
}

int main(int argc, char **argv) {
    int mode = 0; // 'c', 'x', 't'
    int lz4 = 0;
    int quiet = 0;
    const char *archive_name = NULL;
    const char *target_dir = NULL;
    
    char **paths = (char **)malloc(sizeof(char *) * argc);
    if (!paths) {
        printf("tar: out of memory\n");
        return 1;
    }
    int path_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lz4") == 0) {
            lz4 = 1;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "--silent") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-C") == 0) {
            if (i + 1 >= argc) {
                free(paths);
                print_usage();
                return 1;
            }
            target_dir = argv[++i];
        } else if (argv[i][0] == '-') {
            const char *arg = argv[i] + 1;
            for (int j = 0; arg[j]; j++) {
                if (arg[j] == 'c') mode = 'c';
                else if (arg[j] == 'x') mode = 'x';
                else if (arg[j] == 't') mode = 't';
                else if (arg[j] == 'z') lz4 = 1;
                else if (arg[j] == 'q') quiet = 1;
                else if (arg[j] == 'f') {
                    if (i + 1 >= argc) {
                        free(paths);
                        print_usage();
                        return 1;
                    }
                    archive_name = argv[++i];
                    break;
                } else if (arg[j] == 'C') {
                    if (i + 1 >= argc) {
                        free(paths);
                        print_usage();
                        return 1;
                    }
                    target_dir = argv[++i];
                    break;
                } else {
                    free(paths);
                    print_usage();
                    return 1;
                }
            }
        } else {
            paths[path_count++] = argv[i];
        }
    }

    if (mode == 0 || !archive_name) {
        free(paths);
        print_usage();
        return 1;
    }

    if (mode == 'c') {
        if (path_count == 0) {
            free(paths);
            print_usage();
            return 1;
        }
        int ret = create_archive(archive_name, path_count, paths, lz4);
        free(paths);
        return ret;
    }

    if (mode == 'x') {
        free(paths);
        return read_archive(archive_name, 1, lz4, target_dir, quiet);
    }

    if (mode == 't') {
        free(paths);
        return read_archive(archive_name, 0, lz4, target_dir, quiet);
    }

    free(paths);
    print_usage();
    return 1;
}
