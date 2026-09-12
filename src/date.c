// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

int main(int argc, char **argv) {
    tzset();
    bool utc = false;
    const char *fmt = "%a %b %e %H:%M:%S %Z %Y";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--utc") == 0) {
            utc = true;
        } else if (argv[i][0] == '+') {
            fmt = &argv[i][1];
        }
    }
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm = utc ? gmtime_r(&now, &tm_buf) : localtime_r(&now, &tm_buf);
    if (tm) {
        char buf[256];
        strftime(buf, sizeof(buf), fmt, tm);
        printf("%s\n", buf);
        return 0;
    } else {
        printf("Error: Could not retrieve date.\n");
        return 1;
    }
}
