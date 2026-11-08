/* SPDX-License-Identifier: GPL-2.0-only
 * ReRead's stock Colbert fan reset. See analysis/cooling.md.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>

static int matches(const char *path, const char *wanted)
{
    char value[64];
    FILE *file = fopen(path, "r");
    int match = 0;
    if (file) {
        if (fgets(value, sizeof(value), file)) {
            value[strcspn(value, "\n")] = 0;
            match = strcmp(value, wanted) == 0;
        }
        fclose(file);
    }
    return match;
}

static void set_byte(unsigned char address, unsigned char value)
{
    outb(0xf4, 0x1201);
    outb(address, 0x1202);
    outb(value, 0x1203);
}

int main(void)
{
    struct timespec delay = {2, 0}, remaining;
    sigset_t blocked;
    int result;
    if (!matches("/sys/class/dmi/id/sys_vendor", "Intel Corporation") ||
        !matches("/sys/class/dmi/id/product_name", "Colbert")) return 0;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGHUP);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &blocked, NULL) || ioperm(0x1201, 3, 1)) {
        perror("Fan reset: I/O permission");
        return 1;
    }
    set_byte(0x70, 1);
    set_byte(0x72, 0);
    do {
        result = nanosleep(&delay, &remaining);
        if (result && errno == EINTR) delay = remaining;
    } while (result && errno == EINTR);
    set_byte(0x70, 0);
    ioperm(0x1201, 3, 0);
    fputs("Stock fan reset complete; control mode restored to 0.\n", stderr);
    return result ? 1 : 0;
}
