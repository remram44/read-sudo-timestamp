#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/sysmacros.h> /* major/minor */

#define EXIT_MATCH 0
#define EXIT_NO_MATCH 1
#define EXIT_INVALID_USAGE 2
#define EXIT_ERROR 3

char *get_tty_name(void) {
    static char *buf = NULL;
    static ssize_t size = 128;
    if(buf == NULL) {
        buf = malloc(128);
    }

    ssize_t len;
    for(;;) {
        len = readlink("/proc/self/fd/0", buf, size);
        if(len == -1) {
            perror("Readlink on /proc/self/fd/0");
            exit(EXIT_ERROR);
        } else if(len < size) {
            buf[len] = 0;
            return buf;
        }

        size *= 2;
        buf = realloc(buf, size);
    }
}

dev_t get_tty_rdev(void) {
    char *tty_path = get_tty_name();
    if(strncmp(tty_path, "/dev/", 5) != 0) {
        fprintf(stderr, "Invalid tty\n");
        exit(EXIT_ERROR);
    }

    struct stat tty_stat;
    if(stat(tty_path, &tty_stat) != 0) {
        perror("stat on tty");
        exit(EXIT_ERROR);
    }

    return tty_stat.st_rdev;
}

/* https://stackoverflow.com/a/68804612/711380 */
struct timespec diff_timespec(const struct timespec *time1, const struct timespec *time0) {
    struct timespec diff;
    diff.tv_sec = time1->tv_sec - time0->tv_sec;
    diff.tv_nsec = time1->tv_nsec - time0->tv_nsec;
    if(diff.tv_nsec < 0) {
        diff.tv_nsec += 1000000000;
        diff.tv_sec--;
    }
    return diff;
}

unsigned long long get_process_start_time(pid_t process) {
    char path[1024];
    int len = snprintf(path, 1024, "/proc/%u/stat", process);
    if(len == 0 || len >= 1024) {
        fprintf(stderr, "Invalid sid\n");
        exit(EXIT_ERROR);
    }
    FILE *fp = fopen(path, "rb");
    if(fp == NULL) {
        if(errno == ENOENT) {
            return 0;
        }
        perror("Error opening process stat");
        exit(EXIT_ERROR);
    }
    unsigned long long start_time;
    if(fscanf(fp, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %llu ", &start_time) != 1) {
        fprintf(stderr, "Invalid process stat\n");
        exit(EXIT_ERROR);
    }
    return start_time;// / sysconf(_SC_CLK_TCK);
}

#define simpleread(v) \
    do { \
        if(fread(&v, sizeof(v), 1, fp) != 1) { \
            perror("read"); \
            exit(EXIT_ERROR); \
        } \
    } while(0)
#define simpledef(t, v) \
    t v; \
    simpleread(v)

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage: read_sudo_timestamp <timeout>\n");
        return EXIT_INVALID_USAGE;
    }

    int timeout;
    {
        char *endptr;
        timeout = strtol(argv[1], &endptr, 10);
        if(!argv[0] || *endptr || timeout <= 0) {
            fprintf(stderr, "Invalid timeout value\n");
            return EXIT_INVALID_USAGE;
        }
    }

    uid_t target_uid = getuid();

    dev_t target_tty = get_tty_rdev();

    pid_t target_ppid = getppid();

    struct timespec boottime;
    if(clock_gettime(CLOCK_BOOTTIME, &boottime) != 0) {
        perror("clock_gettime");
        exit(EXIT_ERROR);
    }

    char *username = getlogin();

    pid_t target_sid = getsid(0);
    if(target_sid == (pid_t)-1) {
        perror("getsid");
        exit(EXIT_ERROR);
    }

    char path[1024];
    {
        int len = snprintf(path, 1024, "/var/run/sudo/ts/%s", username);
        if(len == 0 || len >= 1024) {
            fprintf(stderr, "Invalid username\n");
            exit(EXIT_ERROR);
        }
    }

    long clock_ticks = sysconf(_SC_CLK_TCK);

    /* setuid! */
    if(setuid(0) != 0) {
        perror("setuid");
        exit(EXIT_ERROR);
    }

    unsigned long long session_start_time = get_process_start_time(target_sid);

    unsigned long long ppid_start_time = get_process_start_time(target_ppid);

    FILE *fp = fopen(path, "rb");
    if(fp == NULL) {
        perror("Opening timestamp file");
        exit(EXIT_ERROR);
    }

    int matches = 0;

    for(;;) {
        unsigned short version;
        if(fread(&version, sizeof(version), 1, fp) != 1) {
            break;
        }
        simpledef(unsigned short, size);

        if(version != 2) {
            fprintf(stderr, "Skipping record with version %u\n", version);
            if(fseek(fp, size, SEEK_CUR) != 0) {
                perror("fseek");
                exit(EXIT_ERROR);
            }
            continue;
        }

        simpledef(unsigned short, type);
        simpledef(unsigned short, flags);
        simpledef(uid_t, auth_uid);
        simpledef(pid_t, sid);

        struct timespec start_time;
        simpleread(start_time.tv_sec);
        simpleread(start_time.tv_nsec);

        struct timespec ts;
        simpleread(ts.tv_sec);
        simpleread(ts.tv_nsec);

        simpledef(union { dev_t ttydev; pid_t ppid; }, u);

        if(type == 4) { /* TS_LOCKEXCL */
            /* Special lock record, skip */
            continue;
        }
        if(type != 1 && type != 2 && type != 3) {
            fprintf(stderr, "Unknown type\n");
            exit(EXIT_ERROR);
        }

        if(flags & 0x01) {
            /* disabled */
            continue;
        }
        int any_uid = flags & 0x02;

        if(!(any_uid || auth_uid == target_uid)) {
            continue;
        }

        ts.tv_sec += timeout;
        struct timespec validity = diff_timespec(&ts, &boottime);
        if(validity.tv_sec < 0) {
            continue;
        }

        if(type == 1) { /* TS_GLOBAL */
            printf("found timestamp, valid for %.2fs\n", validity.tv_sec + validity.tv_nsec * 1e-9);
            matches += 1;
        } else if(type == 2) { /* TS_TTY */
            if(u.ttydev == target_tty && sid == target_sid) {
                /* Check session start_time */
                {
                    long long start_time_clk = start_time.tv_sec * clock_ticks;
                    int multiplier = 1000000000 / clock_ticks;
                    start_time_clk += start_time.tv_nsec / multiplier;

                    long long diff = start_time_clk - session_start_time;
                    if(diff < -1 || diff > 1) {
                        continue;
                    }
                }

                printf("found timestamp for tty %d:%d, valid for %.2fs\n", major(u.ttydev), minor(u.ttydev), validity.tv_sec + validity.tv_nsec * 1e-9);
                matches += 1;
            }
        } else if(type == 3) { /* TS_PPID */
            if(u.ppid == target_ppid) {
                /* Check ppid start_time */
                {
                    long long start_time_clk = start_time.tv_sec * clock_ticks;
                    int multiplier = 1000000000 / clock_ticks;
                    start_time_clk += start_time.tv_nsec / multiplier;

                    long long diff = start_time_clk - ppid_start_time;
                    if(diff < -1 || diff > 1) {
                        continue;
                    }
                }

                printf("found timestamp for ppid %u, valid for %.2fs\n", u.ppid, validity.tv_sec + validity.tv_nsec * 1e-9);
                matches += 1;
            }
        }
    }

    if(matches > 0) {
        return EXIT_MATCH;
    } else {
        return EXIT_NO_MATCH;
    }
}
