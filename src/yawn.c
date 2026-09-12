// Copyright (c) 2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <syscall.h>

#define TIOCSCTTY 0x540E
#define TIOCSPGRP 0x5410

#define SIGHUP   1
#define SIGINT   2
#define SIGKILL  9
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGTERM  15
#define SIGCHLD  17
#define SIGWINCH 28

#define VT_GETACTIVE 0x5607
#define VT_GETSTATE  0x5603

#define VIRTUAL_TTY_BASE 0
#define VIRTUAL_TTY_MAX  9
#define SERIAL_TTY_BASE  10
#define SERIAL_TTY_MAX   13

#define MAX_LINE     512
#define MAX_PATH     256
#define MAX_CONFIGS  64
#define MAX_TTYS     32

typedef enum {
    TTY_MODE_OFF,
    TTY_MODE_RESPAWN,
    TTY_MODE_LAZY,
    TTY_MODE_DYNAMIC
} tty_mode_t;

typedef struct {
    char name[32];          // e.g. "tty1", "ttyS0", "pts"
    char dev_path[64];      // e.g. "/dev/tty1", "/dev/ttyS0"
    char command[MAX_PATH]; // e.g. "/bin/bsh 1"
    char term_type[32];     // e.g. "ansi", "vt100", "xterm"
    bool is_on;
    tty_mode_t mode;
    int tty_id;             // 0-9 for tty1-10, 10-13 for ttyS0-3
    int pid;
    int crash_count;
    time_t first_crash_time;
    time_t backoff_until;
    bool in_backoff;
} tty_entry_t;

static tty_entry_t g_ttys[MAX_TTYS];
static int g_tty_count = 0;

typedef struct {
    char key[64];
    char val[128];
} config_entry_t;

static config_entry_t g_configs[MAX_CONFIGS];
static int g_config_count = 0;

static volatile sig_atomic_t g_reboot_requested   = 0;
static volatile sig_atomic_t g_shutdown_requested = 0;
static volatile sig_atomic_t g_reload_requested   = 0;
static volatile sig_atomic_t g_check_ttys_requested = 0;
static bool g_shutting_down = false;
static bool g_headless_mode = false;

static void handle_sigchld(int sig) {
    (void)sig;
}

static void handle_sigusr1(int sig) {
    (void)sig;
    g_reboot_requested = 1;
}

static void handle_sigusr2(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
}

static void handle_sighup(int sig) {
    (void)sig;
    g_reload_requested = 1;
    g_check_ttys_requested = 1;
}

static void handle_sigwinch(int sig) {
    (void)sig;
    g_check_ttys_requested = 1;
}

static int g_log_fd = -1;
static bool g_log_to_console = true;

static void init_log(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) return;

    if (g_log_fd >= 0) {
        write(g_log_fd, buf, len);
    }
    if (g_log_to_console) {
        write(1, buf, len);
    }
}

static void config_parse_line(char *line) {
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';

    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
    if (!*line) return;

    char *eq = strchr(line, '=');
    if (!eq) return;

    *eq = '\0';
    char *key = line;
    char *val = eq + 1;

    char *kend = key + strlen(key) - 1;
    while (kend > key && (*kend == ' ' || *kend == '\t')) *kend-- = '\0';

    while (*val == ' ' || *val == '\t') val++;
    if (*val == '"' || *val == '\'') {
        char quote = *val++;
        char *vend = strchr(val, quote);
        if (vend) *vend = '\0';
    } else {
        char *vend = val + strlen(val) - 1;
        while (vend >= val && (*vend == ' ' || *vend == '\t' || *vend == '\r' || *vend == '\n')) *vend-- = '\0';
    }

    if (g_config_count < MAX_CONFIGS) {
        size_t klen = strlen(key);
        if (klen >= sizeof(g_configs[g_config_count].key)) klen = sizeof(g_configs[g_config_count].key) - 1;
        memcpy(g_configs[g_config_count].key, key, klen);
        g_configs[g_config_count].key[klen] = '\0';

        size_t vlen = strlen(val);
        if (vlen >= sizeof(g_configs[g_config_count].val)) vlen = sizeof(g_configs[g_config_count].val) - 1;
        memcpy(g_configs[g_config_count].val, val, vlen);
        g_configs[g_config_count].val[vlen] = '\0';
        g_config_count++;
    }
}

static void config_load(void) {
    g_config_count = 0;
    int fd = sys_open("/etc/rc.conf", "r");
    if (fd < 0) return;

    char buf[4096];
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;

    buf[n] = '\0';
    char *p = buf;
    while (*p) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = '\0';
        config_parse_line(p);
        if (!eol) break;
        p = eol + 1;
    }
}

static void safe_copy(char *dst, const char *src, size_t max_size) {
    if (!dst || max_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len >= max_size) len = max_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int tty_name_to_id(const char *name) {
    if (strncmp(name, "ttyS", 4) == 0) {
        int num = atoi(name + 4);
        if (num >= 0 && num <= (SERIAL_TTY_MAX - SERIAL_TTY_BASE)) {
            return SERIAL_TTY_BASE + num;
        }
        return -1;
    }
    if (strncmp(name, "tty", 3) == 0) {
        int num = atoi(name + 3);
        if (num >= 1 && num <= (VIRTUAL_TTY_MAX - VIRTUAL_TTY_BASE + 1)) {
            return VIRTUAL_TTY_BASE + (num - 1);
        }
        return -1;
    }
    return -1;
}

static void ttys_parse_line(char *line) {
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';

    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
    if (!*line) return;

    char name[32] = {0};
    char command[MAX_PATH] = {0};
    char term_type[32] = {0};
    char status_str[16] = {0};
    char mode_str[16] = {0};

    char *p = line;
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(name) - 1) {
        name[i++] = *p++;
    }
    name[i] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        i = 0;
        while (*p && *p != q && i < (int)sizeof(command) - 1) {
            command[i++] = *p++;
        }
        command[i] = '\0';
        if (*p == q) p++;
    } else {
        i = 0;
        while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(command) - 1) {
            command[i++] = *p++;
        }
        command[i] = '\0';
    }

    while (*p == ' ' || *p == '\t') p++;
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(term_type) - 1) {
        term_type[i++] = *p++;
    }
    term_type[i] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(status_str) - 1) {
        status_str[i++] = *p++;
    }
    status_str[i] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < (int)sizeof(mode_str) - 1) {
        mode_str[i++] = *p++;
    }
    mode_str[i] = '\0';

    if (name[0] == '\0') return;

    tty_entry_t *entry = NULL;
    for (int k = 0; k < g_tty_count; k++) {
        if (strcmp(g_ttys[k].name, name) == 0) {
            entry = &g_ttys[k];
            break;
        }
    }

    if (!entry) {
        if (g_tty_count >= MAX_TTYS) return;
        entry = &g_ttys[g_tty_count++];
        memset(entry, 0, sizeof(tty_entry_t));
        safe_copy(entry->name, name, sizeof(entry->name));
        snprintf(entry->dev_path, sizeof(entry->dev_path), "/dev/%s", name);
        entry->tty_id = tty_name_to_id(name);
    }

    safe_copy(entry->command, command[0] ? command : "/bin/bsh.elf", sizeof(entry->command));
    safe_copy(entry->term_type, term_type[0] ? term_type : "ansi", sizeof(entry->term_type));
    entry->is_on = (strcasecmp(status_str, "on") == 0);

    if (strcasecmp(mode_str, "respawn") == 0) {
        entry->mode = TTY_MODE_RESPAWN;
    } else if (strcasecmp(mode_str, "lazy") == 0) {
        entry->mode = TTY_MODE_LAZY;
    } else if (strcasecmp(mode_str, "dynamic") == 0) {
        entry->mode = TTY_MODE_DYNAMIC;
    } else {
        entry->mode = TTY_MODE_OFF;
    }
}

static void ttys_load(void) {
    int fd = sys_open("/etc/ttys", "r");
    if (fd < 0) return;

    char buf[4096];
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;

    buf[n] = '\0';
    char *p = buf;
    while (*p) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = '\0';
        ttys_parse_line(p);
        if (!eol) break;
        p = eol + 1;
    }
}

static int tty_spawn(tty_entry_t *entry) {
    if (!entry || !entry->command[0] || !entry->is_on) return -1;
    if (entry->pid > 0) return entry->pid;

    char cmd_copy[MAX_PATH];
    safe_copy(cmd_copy, entry->command, sizeof(cmd_copy));

    char bin_path[MAX_PATH];
    char *p = cmd_copy;
    while (*p == ' ' || *p == '\t') p++;

    char *space = strpbrk(p, " \t");
    char *args = NULL;
    if (space) {
        *space = '\0';
        args = space + 1;
        while (*args == ' ' || *args == '\t') args++;
    }

    safe_copy(bin_path, p, sizeof(bin_path));
    if (!sys_exists(bin_path)) {
        char elf_path[MAX_PATH + 4];
        snprintf(elf_path, sizeof(elf_path), "%s.elf", bin_path);
        if (sys_exists(elf_path)) {
            safe_copy(bin_path, elf_path, sizeof(bin_path));
        }
    }

    uint64_t flags = SPAWN_FLAG_TERMINAL;
    if (entry->tty_id >= 0) {
        flags |= SPAWN_FLAG_TTY_ID;
    }

    int pid = sys_spawn(bin_path, args, flags, entry->tty_id >= 0 ? entry->tty_id : 0);
    if (pid < 0) {
        init_log("[yawn] sys_spawn failed for %s on %s\n", bin_path, entry->name);
        return -1;
    }

    entry->pid = pid;
    return pid;
}

static void check_lazy_ttys(void) {
    int fd = open("/dev/console", O_RDONLY);
    if (fd < 0) return;

    int active_id = -1;
    ioctl(fd, VT_GETACTIVE, &active_id);

    struct {
        unsigned short v_active;
        unsigned short v_signal;
        unsigned short v_state;
    } vt_st;
    memset(&vt_st, 0, sizeof(vt_st));
    ioctl(fd, VT_GETSTATE, &vt_st);
    uint32_t open_mask = vt_st.v_state;

    close(fd);

    for (int k = 0; k < g_tty_count; k++) {
        tty_entry_t *entry = &g_ttys[k];
        if (!entry->is_on || entry->mode != TTY_MODE_LAZY || entry->pid > 0) continue;

        bool should_spawn = false;
        if (entry->tty_id >= 0) {
            if (entry->tty_id == active_id || (open_mask & (1U << entry->tty_id))) {
                should_spawn = true;
            }
        }

        if (should_spawn) {
            init_log("[yawn] Activating lazy terminal %s (%s)\n", entry->name, entry->command);
            tty_spawn(entry);
        }
    }
}

static void run_script_sync(const char *script_path) {
    if (!sys_exists(script_path)) return;

    const char *shell_bin = "/bin/bsh.elf";
    if (!sys_exists(shell_bin)) {
        shell_bin = "/bin/bsh.elf";
        if (!sys_exists(shell_bin)) return;
    }

    int pid = sys_spawn(shell_bin, script_path, SPAWN_FLAG_TERMINAL | SPAWN_FLAG_INHERIT_TTY, 0);
    if (pid < 0) return;

    int status = 0;
    while (1) {
        int r = sys_waitpid(pid, &status, 0);
        if (r == pid || r < 0) break;
    }
}

static void perform_shutdown(bool is_reboot) {
    g_shutting_down = true;

    signal(SIGCHLD, SIG_DFL);
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    signal(SIGHUP,  SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGWINCH, SIG_IGN);

    int cfd = open("/dev/console", O_WRONLY);
    if (cfd >= 0) {
        dup2(cfd, 1);
        dup2(cfd, 2);
        if (cfd > 2) close(cfd);
    }
    g_log_to_console = true;

    init_log("\n[yawn] %s requested, initiating system shutdown...\n",
             is_reboot ? "Reboot" : "Poweroff");

    run_script_sync("/etc/rc.shutdown");

    init_log(" [ .. ] Terminating running processes (SIGTERM)...\n");
    sys_kill_signal(-1, SIGTERM);

    struct timespec req = { .tv_sec = 0, .tv_nsec = 50000000 }; // 50ms
    for (int i = 0; i < 30; i++) {
        int status;
        int r;
        while ((r = sys_waitpid(-1, &status, 1 /* WNOHANG */)) > 0) {}
        if (r == -1) break;
        nanosleep(&req, NULL);
    }

    init_log(" [ .. ] Sending SIGKILL to remaining processes...\n");
    sys_kill_signal(-1, SIGKILL);

    for (int i = 0; i < 5; i++) {
        int status;
        int r;
        while ((r = sys_waitpid(-1, &status, 1 /* WNOHANG */)) > 0) {}
        if (r == -1) break;
        nanosleep(&req, NULL);
    }

    init_log(" [ OK ] Syncing filesystems...\n");
    sync();

    if (is_reboot) {
        init_log(" [ OK ] Rebooting system now.\n");
        sys_reboot();
    } else {
        init_log(" [ OK ] Powering off system now.\n");
        sys_shutdown();
    }

    while (1) {
        pause();
    }
}

static int handle_client_command(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: yawn [0|6|q|reload|status]\n");
        printf("  0       Power off system\n");
        printf("  6       Reboot system\n");
        printf("  q       Reload /etc/ttys and /etc/rc.conf\n");
        printf("  status  Check yawn status\n");
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "0") == 0 || strcmp(cmd, "poweroff") == 0 || strcmp(cmd, "shutdown") == 0) {
        return sys_kill_signal(1, SIGUSR2);
    } else if (strcmp(cmd, "6") == 0 || strcmp(cmd, "reboot") == 0) {
        return sys_kill_signal(1, SIGUSR1);
    } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "reload") == 0) {
        return sys_kill_signal(1, SIGHUP);
    } else if (strcmp(cmd, "status") == 0) {
        printf("yawn (PID 1) is active. You Awake? Well, Now what?\n");
        return 0;
    }

    printf("yawn: unknown command '%s'\n", cmd);
    return 1;
}

int main(int argc, char **argv) {
    if (getpid() != 1) {
        return handle_client_command(argc, argv);
    }

    if (fcntl(0, F_GETFL) < 0) {
        int cfd = open("/dev/console", O_RDWR);
        if (cfd >= 0) {
            dup2(cfd, 0);
            dup2(cfd, 1);
            dup2(cfd, 2);
            if (cfd > 2) close(cfd);
        }
    }

    signal(SIGCHLD, handle_sigchld);
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGUSR2, handle_sigusr2);
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    signal(SIGHUP,  handle_sighup);
    signal(SIGWINCH, handle_sigwinch);

    if (argc > 1 && strcmp(argv[1], "headless") == 0) {
        g_headless_mode = true;
    }

    mkdir("/tmp", 0777);
    mkdir("/var", 0755);
    mkdir("/var/run", 0755);
    mkdir("/var/log", 0755);
    mkdir("/etc/rc.d", 0755);

    g_log_fd = open("/var/log/yawn.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    g_log_to_console = true;
    init_log("[yawn] BoredOS YAWN (PID 1) started. You Awake? Well, Now what?\n");

    config_load();
    ttys_load();
    init_log("[yawn] Loaded %d configuration entries, %d terminal entries\n",
             g_config_count, g_tty_count);

    init_log("[yawn] Executing startup script /etc/rc...\n");
    run_script_sync("/etc/rc");
    init_log("[yawn] Startup script /etc/rc completed\n");

    int cfd = open("/dev/console", O_RDWR);
    if (cfd >= 0) {
        ioctl(cfd, 0x5609 /* VT_STOPBOOTLOG */, NULL);

        static char boot_log_buf[65536];
        struct {
            char *buf;
            size_t size;
            size_t written;
        } bl = { boot_log_buf, sizeof(boot_log_buf), 0 };

        if (ioctl(cfd, 0x5608 /* VT_GETBOOTLOG */, &bl) == 0 && bl.written > 0) {
            if (g_log_fd >= 0) {
                lseek(g_log_fd, 0, SEEK_SET);
                write(g_log_fd, bl.buf, bl.written);
                ftruncate(g_log_fd, bl.written);
                lseek(g_log_fd, 0, SEEK_END);
            }
        }

        if (!g_headless_mode) {
            ioctl(cfd, 0x5606 /* VT_ACTIVATE */, (void*)(uintptr_t)0);
        }
        close(cfd);
    }

    printf("\x1b[2J\x1b[H");
    fflush(stdout);
    fflush(stderr);

    g_log_to_console = false;
    if (g_log_fd >= 0) {
        dup2(g_log_fd, 1);
        dup2(g_log_fd, 2);
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    init_log("[yawn] Primary terminal active on %s\n",
             g_headless_mode ? "ttyS0" : "tty1");

    if (g_headless_mode) {
        for (int k = 0; k < g_tty_count; k++) {
            if (strcmp(g_ttys[k].name, "ttyS0") == 0) {
                g_ttys[k].is_on = true;
                g_ttys[k].mode = TTY_MODE_RESPAWN;
                break;
            }
        }
    }

    for (int k = 0; k < g_tty_count; k++) {
        tty_entry_t *entry = &g_ttys[k];
        if (!entry->is_on || entry->mode != TTY_MODE_RESPAWN) continue;

        if (g_headless_mode) {
            if (strncmp(entry->name, "ttyS", 4) == 0) {
                tty_spawn(entry);
            }
        } else {
            if (strncmp(entry->name, "ttyS", 4) != 0) {
                tty_spawn(entry);
            }
        }
    }

    while (1) {
        bool has_backoff = false;
        for (int k = 0; k < g_tty_count; k++) {
            if (g_ttys[k].in_backoff && g_ttys[k].pid == 0) {
                has_backoff = true;
                break;
            }
        }

        struct timespec sleep_duration = { .tv_sec = 0, .tv_nsec = (has_backoff ? 500000000 : 250000000) };
        nanosleep(&sleep_duration, NULL);

        time_t now = time(NULL);

        if (g_check_ttys_requested) {
            g_check_ttys_requested = 0;
            check_lazy_ttys();
        }

        if (g_reboot_requested) {
            perform_shutdown(true);
        }
        if (g_shutdown_requested) {
            perform_shutdown(false);
        }

        if (g_reload_requested) {
            g_reload_requested = 0;
            init_log("[yawn] Reloading /etc/ttys and /etc/rc.conf...\n");
            config_load();
            ttys_load();

            for (int k = 0; k < g_tty_count; k++) {
                tty_entry_t *entry = &g_ttys[k];
                if (!entry->is_on && entry->pid > 0) {
                    sys_kill_signal(entry->pid, SIGTERM);
                } else if (entry->is_on && entry->mode == TTY_MODE_RESPAWN && entry->pid == 0) {
                    if (g_headless_mode) {
                        if (strncmp(entry->name, "ttyS", 4) == 0) tty_spawn(entry);
                    } else {
                        if (strncmp(entry->name, "ttyS", 4) != 0) tty_spawn(entry);
                    }
                }
            }
            check_lazy_ttys();
        }

        int status = 0;
        int reaped_pid = 0;
        while ((reaped_pid = sys_waitpid(-1, &status, 1 /* WNOHANG */)) > 0) {
            for (int k = 0; k < g_tty_count; k++) {
                tty_entry_t *entry = &g_ttys[k];
                if (reaped_pid == entry->pid) {
                    entry->pid = 0;

                    if (!g_shutting_down && entry->is_on) {
                        bool should_spawn = false;
                        if (entry->mode == TTY_MODE_RESPAWN) {
                            should_spawn = true;
                        } else if (entry->mode == TTY_MODE_LAZY) {
                            // For lazy TTY: respawn if still active on screen
                            int cfd = open("/dev/console", O_RDONLY);
                            int cur_act = -1;
                            if (cfd >= 0) {
                                ioctl(cfd, VT_GETACTIVE, &cur_act);
                                close(cfd);
                            }
                            if (entry->tty_id == cur_act) {
                                should_spawn = true;
                            }
                        }

                        if (should_spawn) {
                            if (now - entry->first_crash_time < 10) {
                                entry->crash_count++;
                                if (entry->crash_count >= 5) {
                                    entry->in_backoff = true;
                                    entry->backoff_until = now + 5;
                                    init_log("\n[yawn] Terminal %s (%s) exited repeatedly; throttling for 5 seconds\n",
                                             entry->name, entry->command);
                                }
                            } else {
                                entry->crash_count = 1;
                                entry->first_crash_time = now;
                                entry->in_backoff = false;
                            }

                            if (!entry->in_backoff) {
                                tty_spawn(entry);
                            }
                        }
                    }
                    break;
                }
            }
        }

        for (int k = 0; k < g_tty_count; k++) {
            tty_entry_t *entry = &g_ttys[k];
            if (entry->in_backoff && entry->pid == 0) {
                if (now >= entry->backoff_until) {
                    entry->in_backoff = false;
                    entry->crash_count = 0;
                    init_log("[yawn] Resuming respawn for %s\n", entry->name);
                    tty_spawn(entry);
                }
            }
        }
    }

    return 0;
}
