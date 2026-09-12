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
#include <sys/wait.h>
#include <sys/stat.h>
#include <syscall.h>

#define MAX_PATH    256
#define MAX_CONFIGS 64
#define SIGTERM     15

typedef struct {
    char key[64];
    char val[128];
} config_entry_t;

static config_entry_t g_configs[MAX_CONFIGS];
static int g_config_count = 0;

static void print_usage(void) {
    printf("Usage: service <name> [start|stop|restart|status|forcestart]\n");
    printf("       service -e     (list enabled services in /etc/rc.conf)\n");
    printf("       service -l     (list available services in /etc/rc.d)\n");
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

static const char *config_get(const char *key) {
    for (int i = 0; i < g_config_count; i++) {
        if (strcmp(g_configs[i].key, key) == 0) {
            return g_configs[i].val;
        }
    }
    return NULL;
}

static bool config_is_enabled(const char *service_name) {
    char key[80];
    snprintf(key, sizeof(key), "%s_enable", service_name);
    const char *val = config_get(key);
    if (!val) return false;
    return (strcasecmp(val, "YES") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "TRUE") == 0);
}

static int read_pid_file(const char *pid_file) {
    int fd = sys_open(pid_file, "r");
    if (fd < 0) return -1;
    char buf[32];
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

static void write_pid_file(const char *pid_file, int pid) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", pid);
    int fd = sys_open(pid_file, "w");
    if (fd >= 0) {
        sys_write_fs(fd, buf, (int)strlen(buf));
        sys_close(fd);
    }
}

static bool is_pid_alive(int pid) {
    if (pid <= 0) return false;
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
    return sys_exists(proc_path) == 1;
}

static int run_command_sync(const char *bin, const char *args) {
    int pid = sys_spawn(bin, args, SPAWN_FLAG_TERMINAL | SPAWN_FLAG_INHERIT_TTY, 0);
    if (pid < 0) return -1;
    int status = 0;
    while (1) {
        int r = sys_waitpid(pid, &status, 0);
        if (r == pid || r < 0) break;
    }
    return status;
}

static void service_log(const char *msg) {
    int fd = sys_open("/var/log/yawn.log", "a");
    if (fd >= 0) {
        sys_write_fs(fd, msg, (int)strlen(msg));
        sys_close(fd);
    }
}

static int service_daemon(const char *name, const char *action, bool force) {
    char pid_file[MAX_PATH];
    snprintf(pid_file, sizeof(pid_file), "/var/run/%s.pid", name);

    char bin_path[MAX_PATH];
    snprintf(bin_path, sizeof(bin_path), "/bin/%s.elf", name);
    if (!sys_exists(bin_path)) {
        snprintf(bin_path, sizeof(bin_path), "/bin/%s", name);
        if (!sys_exists(bin_path)) {
            printf("service: '%s' not found in /etc/rc.d/ or /bin/\n", name);
            return 1;
        }
    }

    if (strcmp(action, "status") == 0) {
        int pid = read_pid_file(pid_file);
        if (pid > 0 && is_pid_alive(pid)) {
            printf("%s is running as PID %d\n", name, pid);
            return 0;
        } else {
            printf("%s is not running\n", name);
            return 3;
        }
    }

    if (strcmp(action, "stop") == 0 || strcmp(action, "restart") == 0) {
        int pid = read_pid_file(pid_file);
        if (pid > 0 && is_pid_alive(pid)) {
            printf("Stopping %s (PID %d)...\n", name, pid);
            sys_kill_signal(pid, SIGTERM);
            sys_delete(pid_file);
            printf(" [ OK ] %s stopped.\n", name);
            char logbuf[128];
            snprintf(logbuf, sizeof(logbuf), "[yawn] service: stopped %s (PID %d)\n", name, pid);
            service_log(logbuf);
        } else {
            printf("%s is not running\n", name);
            sys_delete(pid_file);
        }
        if (strcmp(action, "stop") == 0) return 0;
    }

    // Start daemon
    if (!force && !config_is_enabled(name)) {
        printf(" [ .. ] %s is disabled in /etc/rc.conf\n", name);
        return 0;
    }

    int existing_pid = read_pid_file(pid_file);
    if (existing_pid > 0 && is_pid_alive(existing_pid)) {
        printf("%s is already running as PID %d\n", name, existing_pid);
        return 0;
    }

    char flags_key[80];
    snprintf(flags_key, sizeof(flags_key), "%s_flags", name);
    const char *flags = config_get(flags_key);

    printf("Starting %s...\n", name);
    int pid = sys_spawn(bin_path, flags ? flags : "", SPAWN_FLAG_BACKGROUND, 0);
    if (pid > 0) {
        write_pid_file(pid_file, pid);
        printf(" [ OK ] %s started (PID %d).\n", name, pid);
        char logbuf[128];
        snprintf(logbuf, sizeof(logbuf), "[yawn] service: started %s (PID %d)\n", name, pid);
        service_log(logbuf);
        return 0;
    } else {
        printf(" [FAIL] Failed to start %s\n", name);
        return 1;
    }
}

static void list_enabled(void) {
    config_load();
    printf("Enabled services (/etc/rc.conf):\n");
    for (int i = 0; i < g_config_count; i++) {
        if (strstr(g_configs[i].key, "_enable")) {
            const char *val = g_configs[i].val;
            if (strcasecmp(val, "YES") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "TRUE") == 0) {
                char svc[64];
                size_t slen = strlen(g_configs[i].key);
                if (slen >= sizeof(svc)) slen = sizeof(svc) - 1;
                memcpy(svc, g_configs[i].key, slen);
                svc[slen] = '\0';
                char *sub = strstr(svc, "_enable");
                if (sub) *sub = '\0';
                printf("  - %s\n", svc);
            }
        }
    }
}

static void list_available(void) {
    printf("Available services (/etc/rc.d):\n");
    FAT32_FileInfo entries[64];
    int count = sys_list("/etc/rc.d", entries, 64);
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            if (entries[i].name[0] == '.') continue;
            printf("  - %s\n", entries[i].name);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-e") == 0) {
        list_enabled();
        return 0;
    }
    if (strcmp(argv[1], "-l") == 0) {
        list_available();
        return 0;
    }

    config_load();

    const char *service_name = argv[1];
    const char *action = (argc >= 3) ? argv[2] : "status";
    bool force = (strcmp(action, "forcestart") == 0 || strcmp(action, "onestart") == 0);

    char rc_script[MAX_PATH];
    snprintf(rc_script, sizeof(rc_script), "/etc/rc.d/%s", service_name);
    if (sys_exists(rc_script)) {
        const char *shell_bin = "/bin/bsh.elf";
        if (!sys_exists(shell_bin)) {
            shell_bin = "/bin/bsh.elf";
            if (!sys_exists(shell_bin)) {
                printf("service: cannot execute script: shell not found\n");
                return 1;
            }
        }
        char cmd_args[MAX_PATH + 64];
        snprintf(cmd_args, sizeof(cmd_args), "%s %s", rc_script, action);
        return run_command_sync(shell_bin, cmd_args);
    }

    if (force) action = "start";

    return service_daemon(service_name, action, force);
}
