// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Show command and system help.
#include <stdlib.h>
#include <syscall.h>
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *env_hc = getenv("help_color");
    uint64_t help_color = 0;
    if (env_hc && env_hc[0] == '0' && (env_hc[1] == 'x' || env_hc[1] == 'X')) {
        uint32_t val = 0;
        int i = 2;
        while (env_hc[i]) {
            char c = env_hc[i];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
            if (d < 0) break;
            val = (val << 4) | d;
            i++;
        }
        if (i == 10) help_color = val;
        else if (i == 8) help_color = 0xFF000000 | val;
    }
    if (help_color == 0) help_color = 0xFF569CD6; 
    sys_set_text_color(help_color);

    printf("BoredOS CLI Help\n");
    printf("---------------------------\n");
    printf("ls [path]      - List directory contents\n");
    printf("cd <path>      - Change current directory (built-in)\n");
    printf("pwd            - Print current directory\n");
    printf("mkdir <dir>    - Create directory\n");
    printf("rm <path>      - Remove file or directory\n");
    printf("cat <file>     - Print file contents\n");
    printf("echo [text]    - Print text\n");
    printf("touch <file>   - Create empty file\n");
    printf("cp <src> <dst> - Copy file\n");
    printf("mv <src> <dst> - Move file\n");
    printf("date           - Print current date and time\n");
    printf("uptime         - Print system uptime\n");
    printf("meminfo        - Print memory information\n");
    printf("hexdump <file> - Display file contents in hexadecimal.\n");
    printf("ps [options]   - List running processes\n");
    printf("kill <pid>     - Terminate a process\n");
    printf("lsblk          - List block devices and partitions\n");
    printf("cowsay [msg]   - Fun cow says something\n");
    printf("beep           - Make a beep sound\n");
    printf("reboot         - Reboot the system\n");
    printf("shutdown       - Shutdown the system\n");
    printf("sysfetch       - Show system information\n");
    printf("tcc <file.c>   - Tiny C Compiler\n");
    printf("man <cmd>      - Show manual page\n");
    printf("clear          - Clear the screen\n");
    printf("exit           - Exit the terminal\n");
    printf("net            - Network tools\n");
    printf("time <cmd>     - Measure command execution time\n");
    printf("find           - find files or directories\n");
    printf("rev            - Reverse a string or file\n");
    printf("head           - print lines from the top down\n");
    printf("tail           - print lines from the bottom up\n");
    printf("tar <args>     - Create a tar archive\n");
    printf("kilo <file>    - Simple text editor\n");
    printf("loadkeys <id>  - Set keyboard layout (e.g. fr, en, azerty, qwerty)\n");
    printf("\nHint: Use Ctrl+C to force quit any running application.\n");
    return 0;
}
