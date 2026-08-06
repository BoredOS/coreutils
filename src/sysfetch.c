// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <syscall.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_ASCII_LINES 512
#define MAX_ASCII_WIDTH 512
#define MAX_INFO_LINES 32



typedef struct {
    char ascii_art_file[256];
    char user_host_string[64];
    char separator[64];
    char os_label[32];
    char kernel_label[32];
    char uptime_label[32];
    char ip_label[32];
    char shell_label[32];
    char memory_label[32];
    char cpu_label[32];
    char date_label[32];
} SysfetchConfig;

static SysfetchConfig config;
static char ascii_lines[MAX_ASCII_LINES][MAX_ASCII_WIDTH];
static int ascii_line_count = 0;

static uint32_t get_default_text_color(void) {
    const char *env_def = getenv("default_text_color");
    if (env_def && env_def[0] == '0' && (env_def[1] == 'x' || env_def[1] == 'X')) {
        uint32_t val = 0;
        int i = 2;
        while (env_def[i]) {
            char c = env_def[i];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
            if (d < 0) break;
            val = (val << 4) | d;
            i++;
        }
        if (i == 10) return val;
        if (i == 8) return 0xFF000000 | val;
    }
    return 0xFFCCCCCC;
}

static uint32_t ansi_to_boredos_color(int code) {
    uint32_t default_color = get_default_text_color();

    switch (code) {
        case 0: return default_color;
        case 30: return 0xFF000000; // Black
        case 31: return 0xFFFF4444; // Red
        case 32: return 0xFF6A9955; // Green
        case 33: return 0xFFFFFF00; // Yellow
        case 34: return 0xFF569CD6; // Blue
        case 35: return 0xFFB589D6; // Magenta
        case 36: return 0xFF4EC9B0; // Cyan
        case 37: return 0xFFCCCCCC; // White
        case 90: return 0xFF808080; // Bright Black (Gray)
        case 91: return 0xFFFF6B6B; // Bright Red
        case 92: return 0xFF78DE78; // Bright Green
        case 93: return 0xFFFFFF55; // Bright Yellow
        case 94: return 0xFF87CEEB; // Bright Blue
        case 95: return 0xFFFF77FF; // Bright Magenta
        case 96: return 0xFF66D9EF; // Bright Cyan
        case 97: return 0xFFFFFFFF; // Bright White
        case 38: return 0xFF473ba3; // Bored Blue
        default: return default_color;
    }
}

static uint32_t rgb_to_color(int r, int g, int b) {
    return 0xFF000000 | ((uint32_t)(r & 0xFF) << 16) | ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(b & 0xFF);
}

static void printf_ansi(const char *str) {
    uint32_t original_color = get_default_text_color();

    while (*str) {
        bool is_escape = false;
        if (*str == '\033' && *(str + 1) == '[') {
            str += 2;
            is_escape = true;
        } else if (*str == '\\' && *(str + 1) == '0' && *(str + 2) == '3' && *(str + 3) == '3' && *(str + 4) == '[') {
            str += 5;
            is_escape = true;
        }

        if (is_escape) {
            int params[8];
            int param_count = 0;
            int value = 0;
            bool have_digit = false;

            while (*str && *str != 'm') {
                if (*str >= '0' && *str <= '9') {
                    value = value * 10 + (*str - '0');
                    have_digit = true;
                    str++;
                } else if (*str == ';') {
                    if (param_count < 8) params[param_count++] = have_digit ? value : 0;
                    value = 0;
                    have_digit = false;
                    str++;
                } else {
                    break; // malformed sequence, stop parsing params
                }
            }
            if (param_count < 8) params[param_count++] = have_digit ? value : 0;

            if (*str == 'm') {
                str++;
                if (param_count >= 5 && params[0] == 38 && params[1] == 2) {
                    // 24-bit truecolor foreground: \033[38;2;R;G;Bm
                    set_text_color(rgb_to_color(params[2], params[3], params[4]));
                } else if (param_count >= 5 && params[0] == 48 && params[1] == 2) {
                    // 24-bit truecolor background: not supported by set_text_color, ignore
                } else {
                    set_text_color(ansi_to_boredos_color(params[0]));
                }
            }
        } else {
            putchar(*str);
            str++;
        }
    }
    set_text_color(original_color);
}

static int strlen_ansi(const char *str) {
    int len = 0;
    while (*str) {
        if (*str == '\033' && *(str + 1) == '[') {
            str += 2;
            while (*str && *str != 'm') str++;
            if (*str == 'm') str++;
        } else if (*str == '\\' && *(str + 1) == '0' && *(str + 2) == '3' && *(str + 3) == '3' && *(str + 4) == '[') {
            str += 5;
            while (*str && *str != 'm') str++;
            if (*str == 'm') str++;
        } else {
            len++;
            str++;
        }
    }
    return len;
}

static char* trim(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    *(end + 1) = 0;
    return str;
}

static void set_config_defaults() {
    strcpy(config.ascii_art_file, "/Library/AppData/org.boredos.sysfetch/boredos.txt");
    strcpy(config.user_host_string, "auto");
    strcpy(config.separator, "auto");
    strcpy(config.os_label, "OS");
    strcpy(config.kernel_label, "Kernel");
    strcpy(config.uptime_label, "Uptime");
    strcpy(config.ip_label, "Local IP");
    strcpy(config.shell_label, "Shell");
    strcpy(config.memory_label, "Memory");
    strcpy(config.cpu_label, "CPU");
    strcpy(config.date_label, "Date");
}

static void parse_config(char* buffer) {
    char *line = buffer;
    while (*line) {
        char *next_line = strchr(line, '\n');
        if (next_line) *next_line = 0;

        if (line[0] != '/' && line[0] != '\0') {
            char *key = line;
            char *val = strchr(line, '=');
            if (val) {
                *val = 0;
                val++;
                key = trim(key);
                val = trim(val);

                if (strcmp(key, "ascii_art_file") == 0) strcpy(config.ascii_art_file, val);
                else if (strcmp(key, "user_host_string") == 0) strcpy(config.user_host_string, val);
                else if (strcmp(key, "separator") == 0) strcpy(config.separator, val);
                else if (strcmp(key, "os_label") == 0) strcpy(config.os_label, val);
                else if (strcmp(key, "kernel_label") == 0) strcpy(config.kernel_label, val);
                else if (strcmp(key, "uptime_label") == 0) strcpy(config.uptime_label, val);
                else if (strcmp(key, "ip_label") == 0) strcpy(config.ip_label, val);
                else if (strcmp(key, "shell_label") == 0) strcpy(config.shell_label, val);
                else if (strcmp(key, "memory_label") == 0) strcpy(config.memory_label, val);
                else if (strcmp(key, "cpu_label") == 0) strcpy(config.cpu_label, val);
                else if (strcmp(key, "date_label") == 0) strcpy(config.date_label, val);
            }
        }

        if (next_line) line = next_line + 1;
        else break;
    }
}

static void load_config() {
    set_config_defaults();
    int fd = sys_open("/Library/AppData/org.boredos.sysfetch/sysfetch.cfg", 0);
    if (fd < 0) return;

    size_t cap = 16384;
    char *buffer = malloc(cap);
    if (!buffer) {
        sys_close(fd);
        return;
    }

    int total_bytes = 0;
    while (total_bytes < (int)cap - 1) {
        int bytes = sys_read(fd, buffer + total_bytes, cap - 1 - total_bytes);
        if (bytes <= 0) break;
        total_bytes += bytes;
    }
    sys_close(fd);

    if (total_bytes > 0) {
        buffer[total_bytes] = 0;
        parse_config(buffer);
    }
    free(buffer);
}

static void load_ascii_art() {
    int fd = sys_open(config.ascii_art_file, 0);
    if (fd < 0) {
        strcpy(ascii_lines[0], "               \033[38;2;77;81;110m##\033[38;2;39;39;39m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%\033[0m           ");
        strcpy(ascii_lines[1], "              \033[38;2;77;81;110m####\033[38;2;39;39;39m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%\033[0m              ");
        strcpy(ascii_lines[2], "             \033[38;2;77;81;110m##%#%#\033[38;2;39;39;39m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%\033[0m             ");
        strcpy(ascii_lines[3], "            \033[38;2;77;81;110m####%#%%\033[38;2;39;39;39m@@@@@@@\033[38;2;77;81;110m%%%%%%%%%%%%%%\033[38;2;39;39;39m@@@@@@@%\033[0m            ");
        strcpy(ascii_lines[4], "           \033[38;2;77;81;110m###%#%%%%%\033[38;2;39;39;39m@@@@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;39;39;39m@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;39;39;39m@@@@@@%\033[0m           ");
        strcpy(ascii_lines[5], "          \033[38;2;77;81;110m##%#%%#%%%%%\033[38;2;39;39;39m@@@@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;39;39;39m@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;39;39;39m@@@@@@%\033[0m          ");
        strcpy(ascii_lines[6], "         \033[38;2;77;81;110m#####%\033[38;2;255;255;255m###\033[38;2;77;81;110m%%%%%%\033[38;2;39;39;39m@@@@@@@\033[38;2;77;81;110m%%%%%%\033[38;2;39;39;39m@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;39;39;39m@@@@@@%\033[0m         ");
        strcpy(ascii_lines[7], "        \033[38;2;77;81;110m###%#%\033[38;2;255;255;255m#####\033[38;2;77;81;110m%%%%%%\033[38;2;39;39;39m@@@@@@@\033[38;2;77;81;110m%%%%%%\033[38;2;39;39;39m@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;39;39;39m@@@@@@%\033[0m        ");
        strcpy(ascii_lines[8], "      \033[38;2;77;81;110m####%#%\033[38;2;255;255;255m#######\033[38;2;77;81;110m%%%%%%\033[38;2;39;39;39m@@@@@@@\033[38;2;77;81;110m%%%%%%\033[38;2;39;39;39m@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;39;39;39m@@@@@@%\033[0m      ");
        strcpy(ascii_lines[9], "     \033[38;2;77;81;110m####%#%\033[38;2;255;255;255m#######\033[38;2;77;81;110m#%%%%%%%\033[38;2;39;39;39m@@@@@@@\033[38;2;77;81;110m%%%%%%%%%%%%%%%%\033[38;2;39;39;39m@@@@@@@\033[0m     ");
        strcpy(ascii_lines[10], "    \033[38;2;77;81;110m###%#%#\033[38;2;255;255;255m#######\033[38;2;77;81;110m%%%%%%%%%%\033[38;2;39;39;39m@@@@@@@@@\033[38;2;77;81;110m%%%%%%%%%%%\033[38;2;39;39;39m@@@@@@@@@@\033[0m    ");
        strcpy(ascii_lines[11], "   \033[38;2;77;81;110m####%#%\033[38;2;255;255;255m########\033[38;2;77;81;110m%%%%%%%%%%%\033[38;2;39;39;39m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m   ");
        strcpy(ascii_lines[12], "  \033[38;2;77;81;110m###%#%#\033[38;2;255;255;255m##########\033[38;2;77;81;110m%%%%%%%%%%%\033[38;2;39;39;39m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m  ");
        strcpy(ascii_lines[13], " \033[38;2;77;81;110m#####%#\033[38;2;255;255;255m######\033[38;2;77;81;110m%%\033[38;2;255;255;255m####\033[38;2;77;81;110m%%%%%%%%%%\033[38;2;17;17;17m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m ");
        strcpy(ascii_lines[14], " \033[38;2;77;81;110m#%#%%%\033[38;2;255;255;255m#####\033[38;2;77;81;110m%%%\033[38;2;255;255;255m#####\033[38;2;77;81;110m%%%%%%%%%\033[38;2;17;17;17m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m  ");
        strcpy(ascii_lines[15], "   \033[38;2;77;81;110m#%#%%%\033[38;2;255;255;255m###\033[38;2;77;81;110m%%%\033[38;2;255;255;255m#####\033[38;2;77;81;110m%%%%%%%%\033[38;2;17;17;17m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m   ");
        strcpy(ascii_lines[16], "    \033[38;2;77;81;110m%%%%%%\033[38;2;255;255;255m###\033[38;2;77;81;110m%\033[38;2;255;255;255m######\033[38;2;77;81;110m%%%%%%%\033[38;2;17;17;17m@@@@@@@@@@\033[38;2;77;81;110m%%%%%%%%%%%%%\033[38;2;17;17;17m@@@@@@@\033[0m    ");
        strcpy(ascii_lines[17], "     \033[38;2;77;81;110m%%%%%%\033[38;2;255;255;255m#########\033[38;2;77;81;110m%%%%%%\033[38;2;17;17;17m@@@@@@@\033[38;2;77;81;110m%%%%%%%%%%%%%%%%\033[38;2;17;17;17m@@@@@@@\033[0m     ");
        strcpy(ascii_lines[18], "      \033[38;2;77;81;110m%%%%%%\033[38;2;255;255;255m#######\033[38;2;77;81;110m%%%%%%\033[38;2;17;17;17m@@@@@@@\033[38;2;77;81;110m%%%%%%\033[38;2;17;17;17m@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;17;17;17m@@@@@@@\033[0m      ");
        strcpy(ascii_lines[19], "        \033[38;2;77;81;110m%%%%%\033[38;2;255;255;255m#####\033[38;2;77;81;110m%%%%%%\033[38;2;17;17;17m@@@@@@@\033[38;2;77;81;110m%%%%%%%%%%\033[38;2;17;17;17m@@@@@@@@@@@@\033[0m        ");
        strcpy(ascii_lines[20], "         \033[38;2;77;81;110m%%%%%%%%%%%%%%\033[38;2;17;17;17m@@@@@@@@\033[38;2;77;81;110m%%%%%%%%%%%%%%%\033[38;2;17;17;17m@@@@@@\033[0m         ");
        strcpy(ascii_lines[21], "          \033[38;2;77;81;110m%%%%%%%%%%%%\033[38;2;17;17;17m@@@@@@@@@@@@@@@\033[38;2;77;81;110m%%%%%%%%\033[38;2;17;17;17m@@@@@@\033[0m          ");
        strcpy(ascii_lines[22], "           \033[38;2;77;81;110m%%%%%%%%%%\033[38;2;17;17;17m@@@@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;17;17;17m@@@\033[38;2;77;81;110m%%%%%%%\033[38;2;17;17;17m@@@@@@\033[0m           ");
        strcpy(ascii_lines[23], "            \033[38;2;77;81;110m%%%%%%%%\033[38;2;17;17;17m@@@@@@\033[38;2;77;81;110m%%%%%%%%%%%%%%%%\033[38;2;17;17;17m@@@@@@@\033[0m            ");
        strcpy(ascii_lines[24], "             \033[38;2;77;81;110m%%%%%%\033[38;2;17;17;17m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m             ");
        strcpy(ascii_lines[25], "              \033[38;2;77;81;110m%%%%\033[38;2;17;17;17m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m              ");
        strcpy(ascii_lines[26], "               \033[38;2;77;81;110m%%\033[38;2;17;17;17m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m");
        ascii_line_count = 27;
        return;
    }

    size_t cap = 65536;
    char *buffer = malloc(cap);
    if (!buffer) {
        sys_close(fd);
        return;
    }

    int total_bytes = 0;
    while (total_bytes < (int)cap - 1) {
        int bytes = sys_read(fd, buffer + total_bytes, cap - 1 - total_bytes);
        if (bytes <= 0) break;
        total_bytes += bytes;
    }
    sys_close(fd);

    if (total_bytes > 0) {
        buffer[total_bytes] = 0;
        char *line = buffer;
        while (*line && ascii_line_count < MAX_ASCII_LINES) {
            char *next_line = strchr(line, '\n');
            if (next_line) *next_line = 0;

            char *cr = strchr(line, '\r');
            if (cr) *cr = 0;
            
            strncpy(ascii_lines[ascii_line_count], line, MAX_ASCII_WIDTH - 1);
            ascii_lines[ascii_line_count][MAX_ASCII_WIDTH - 1] = 0;
            ascii_line_count++;

            if (next_line) line = next_line + 1;
            else break;
        }
    }
    free(buffer);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    load_config();
    load_ascii_art();

    char info_lines[MAX_INFO_LINES][128];
    char temp_buf[32];
    int info_line_count = 0;

    char host_name[64] = "";
    FILE *f_hn = fopen("/etc/hostname", "r");
    if (f_hn) {
        if (fgets(host_name, sizeof(host_name), f_hn)) {
            char *nl = strchr(host_name, '\n'); if (nl) *nl = 0;
            char *cr = strchr(host_name, '\r'); if (cr) *cr = 0;
        }
        fclose(f_hn);
    }

    if (config.user_host_string[0]) {
        if (strcmp(config.user_host_string, "auto") == 0 || strcmp(config.user_host_string, "root@boredos") == 0) {
            if (host_name[0]) {
                snprintf(info_lines[info_line_count++], 127, "root@%s", host_name);
            } else {
                snprintf(info_lines[info_line_count++], 127, "root");
            }
        } else if (strstr(config.user_host_string, "%h")) {
            char user_host_buf[128] = "";
            char *hpos = strstr(config.user_host_string, "%h");
            int prefix_len = hpos - config.user_host_string;
            strncpy(user_host_buf, config.user_host_string, prefix_len);
            user_host_buf[prefix_len] = '\0';
            strcat(user_host_buf, host_name);
            strcat(user_host_buf, hpos + 2);
            strcpy(info_lines[info_line_count++], user_host_buf);
        } else {
            strcpy(info_lines[info_line_count++], config.user_host_string);
        }
    }
    if (config.separator[0]) {
        int host_len = strlen(info_lines[0]);
        if (host_len > 120) host_len = 120;
        char sep_buf[128];
        memset(sep_buf, '-', host_len);
        sep_buf[host_len] = '\0';
        strcpy(info_lines[info_line_count++], sep_buf);
    }
    // Helper for proc parsing
    auto int find_v(const char *b, const char *k) {
        char *p = (char*)b; int kl = strlen(k);
        while (*p) {
            if (memcmp(p, k, kl) == 0 && (p[kl] == ':' || p[kl] == '\t')) {
                p += kl; while (*p == ':' || *p == '\t' || *p == ' ') p++;
                return atoi(p);
            }
            while (*p && *p != '\n') p++; if (*p == '\n') p++;
        }
        return 0;
    }

    auto void find_s(const char *b, const char *k, char *out) {
        char *p = (char*)b; int kl = strlen(k);
        while (*p) {
            if (memcmp(p, k, kl) == 0 && (p[kl] == ':' || p[kl] == '\t')) {
                p += kl; while (*p == ':' || *p == '\t' || *p == ' ') p++;
                int i = 0;
                while (*p && *p != '\n' && i < 127) out[i++] = *p++;
                out[i] = 0;
                return;
            }
            while (*p && *p != '\n') p++; if (*p == '\n') p++;
        }
        strcpy(out, "Unknown");
    }

    int fd_v = sys_open("/proc/version", 0);
    char v_buf[512];
    if (fd_v >= 0) {
        int b = sys_read(fd_v, v_buf, 511);
        v_buf[b] = 0;
        sys_close(fd_v);
    } else strcpy(v_buf, "Unknown");

    if (config.os_label[0]) {
        strcpy(info_lines[info_line_count], config.os_label);
        strcat(info_lines[info_line_count], ": ");
        // Parse "BoredOS [codename] Version X.Y.Z"
        strcat(info_lines[info_line_count], v_buf);
        // Truncate at newline
        char *nl = strchr(info_lines[info_line_count], '\n');
        if (nl) *nl = 0;
        info_line_count++;
    }
    if (config.kernel_label[0]) {
        strcpy(info_lines[info_line_count], config.kernel_label);
        strcat(info_lines[info_line_count], ": ");
        char *kstart = strchr(v_buf, '\n');
        if (kstart) {
            const char *kval = kstart + 1;
            if (strncmp(kval, "Kernel: ", 8) == 0) kval += 8;
            strcat(info_lines[info_line_count], kval);
            char *knext = strchr(info_lines[info_line_count], '\n');
            if (knext) *knext = 0;
        } else strcat(info_lines[info_line_count], "Unknown");
        info_line_count++;
    }
    if (config.uptime_label[0]) {
        int fd_u = sys_open("/proc/uptime", 0);
        if (fd_u >= 0) {
            char u_buf[64];
            int b = sys_read(fd_u, u_buf, 63);
            u_buf[b] = 0;
            sys_close(fd_u);
            int sec  = atoi(u_buf);
            int days = sec / 86400;
            int hrs  = (sec % 86400) / 3600;
            int mins = (sec % 3600) / 60;
            strcpy(info_lines[info_line_count], config.uptime_label);
            strcat(info_lines[info_line_count], ": ");
            if (days > 0) {
                itoa(days, temp_buf); strcat(info_lines[info_line_count], temp_buf);
                strcat(info_lines[info_line_count], days == 1 ? " day, " : " days, ");
            }
            if (hrs > 0 || days > 0) {
                itoa(hrs, temp_buf); strcat(info_lines[info_line_count], temp_buf);
                strcat(info_lines[info_line_count], hrs == 1 ? " hour, " : " hours, ");
            }
            itoa(mins, temp_buf); strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count++], mins == 1 ? " min" : " mins");
        }
    }
    if (config.ip_label[0]) {
        char ip_buf[64] = "0.0.0.0";
        char nic_buf[32] = "";

        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s >= 0) {
            char buf[256];
            struct {
                int ifc_len;
                char pad[4];
                char *ifc_buf;
            } ifc;
            ifc.ifc_len = sizeof(buf);
            ifc.ifc_buf = buf;

            if (ioctl(s, 0x8912, &ifc) == 0 && ifc.ifc_len > 0) {
                struct {
                    char ifr_name[16];
                    uint16_t sa_family;
                    uint16_t sa_port;
                    uint32_t sin_addr;
                    char zero[8];
                } *req = (void *)buf;

                strncpy(nic_buf, req->ifr_name, sizeof(nic_buf) - 1);
                struct in_addr in;
                in.s_addr = req->sin_addr;
                inet_ntop(AF_INET, &in, ip_buf, sizeof(ip_buf));
            }
            close(s);
        }

        strcpy(info_lines[info_line_count], config.ip_label);
        if (nic_buf[0] != 0) {
            strcat(info_lines[info_line_count], " (");
            strcat(info_lines[info_line_count], nic_buf);
            strcat(info_lines[info_line_count], ")");
        }
        strcat(info_lines[info_line_count], ": ");
        strcat(info_lines[info_line_count], ip_buf[0] ? ip_buf : "0.0.0.0");
        info_line_count++;
    }
    if (config.cpu_label[0]) {
        int fd_c = sys_open("/proc/cpuinfo", 0);
        if (fd_c >= 0) {
            char c_buf[2048];
            int b = sys_read(fd_c, c_buf, 2047);
            c_buf[b] = 0;
            sys_close(fd_c);
            
            char model[128];
            find_s(c_buf, "model name", model);
            int cores = find_v(c_buf, "cpu cores");
            
            strcpy(info_lines[info_line_count], config.cpu_label);
            strcat(info_lines[info_line_count], ": ");
            strcat(info_lines[info_line_count], model);
            if (cores > 0) {
                strcat(info_lines[info_line_count], " (");
                itoa(cores, temp_buf);
                strcat(info_lines[info_line_count], temp_buf);
                strcat(info_lines[info_line_count], ")");
            }
            info_line_count++;
        }
    }
    if (config.shell_label[0]) {
        strcpy(info_lines[info_line_count], config.shell_label);
        strcat(info_lines[info_line_count++], ": bsh");
    }
    if (config.memory_label[0]) {
        int fd_m = sys_open("/proc/meminfo", 0);
        if (fd_m >= 0) {
            char m_buf[512];
            int b = sys_read(fd_m, m_buf, 511);
            m_buf[b] = 0;
            sys_close(fd_m);
            int total = find_v(m_buf, "MemTotal");
            int used = find_v(m_buf, "MemUsed");
            strcpy(info_lines[info_line_count], config.memory_label);
            strcat(info_lines[info_line_count], ": ");
            itoa(used / 1024, temp_buf);
            strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count], "MiB / ");
            itoa(total / 1024, temp_buf);
            strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count++], "MiB");
        }
    }
    if (config.date_label[0]) {
        int fd_d = sys_open("/proc/datetime", 0);
        if (fd_d >= 0) {
            char d_buf[64];
            int b = sys_read(fd_d, d_buf, 63);
            d_buf[b] = 0;
            sys_close(fd_d);
            
            strcpy(info_lines[info_line_count], config.date_label);
            strcat(info_lines[info_line_count], ": ");
            strcat(info_lines[info_line_count], d_buf);
            char *nl = strchr(info_lines[info_line_count], '\n');
            if (nl) *nl = 0;
            info_line_count++;
        }
    }

    int max_lines = (ascii_line_count > info_line_count) ? ascii_line_count : info_line_count;
    int ascii_width = 0;
    for (int i = 0; i < ascii_line_count; i++) {
        int len = strlen_ansi(ascii_lines[i]);
        if (len > ascii_width) ascii_width = len;
    }

    for (int i = 0; i < max_lines; i++) {
        if (i < ascii_line_count) {
            printf_ansi(ascii_lines[i]);
            int padding = ascii_width - strlen_ansi(ascii_lines[i]);
            for(int j=0; j<padding; j++) printf(" ");
        } else {
            for(int j=0; j<ascii_width; j++) printf(" ");
        }

        printf("   ");

        if (i < info_line_count) {
            printf_ansi(info_lines[i]);
        }
        printf("\n");
    }

    return 0;
}