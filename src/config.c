#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#include "config.h"

/* ── Global config (definitions) ────────────────────────────────── */
int     cfg_mode            = DEFAULT_MODE;
int     cfg_deadzone        = DEFAULT_DEADZONE;
double  cfg_gain            = DEFAULT_GAIN;
double  cfg_exponent        = DEFAULT_EXPONENT;
double  cfg_max_speed       = DEFAULT_MAX_SPEED;
double  cfg_position_factor = DEFAULT_POSITION_FACTOR;
int     cfg_tick_ms         = DEFAULT_TICK_MS;
int     cfg_horizontal      = DEFAULT_HORIZONTAL;
int     cfg_invert_v        = DEFAULT_INVERT_V;
int     cfg_invert_h        = DEFAULT_INVERT_H;
char   *cfg_match_name      = NULL;
char   *cfg_device_path     = NULL;
int     cfg_foreground      = 0;
int     cfg_verbose         = 0;
int     cfg_list_devices    = 0;

void config_set_defaults(void)
{
    cfg_mode            = DEFAULT_MODE;
    cfg_deadzone        = DEFAULT_DEADZONE;
    cfg_gain            = DEFAULT_GAIN;
    cfg_exponent        = DEFAULT_EXPONENT;
    cfg_max_speed       = DEFAULT_MAX_SPEED;
    cfg_position_factor = DEFAULT_POSITION_FACTOR;
    cfg_tick_ms         = DEFAULT_TICK_MS;
    cfg_horizontal      = DEFAULT_HORIZONTAL;
    cfg_invert_v        = DEFAULT_INVERT_V;
    cfg_invert_h        = DEFAULT_INVERT_H;
    free(cfg_match_name);  cfg_match_name = NULL;
    free(cfg_device_path); cfg_device_path = NULL;
    cfg_foreground      = 0;
    cfg_verbose         = 0;
    cfg_list_devices    = 0;
}

/* ── Simple INI-style parser ────────────────────────────────────── */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

/* ── Bounds-checked numeric parsers ──────────────────────────────── */
static int parse_int(const char *s, int *out, int min, int max, const char *name)
{
    if (!s || !*s) { fprintf(stderr, "[autoscroll] config: %s missing\n", name); return -1; }
    char *end;
    long val = strtol(s, &end, 10);
    if (*end || end == s) {
        fprintf(stderr, "[autoscroll] config: %s '%s' is not a number\n", name, s);
        return -1;
    }
    if (val < min || val > max) {
        fprintf(stderr, "[autoscroll] config: %s=%ld out of range [%d,%d]\n", name, val, min, max);
        return -1;
    }
    *out = (int)val;
    return 0;
}

static int parse_double(const char *s, double *out, double min, double max, const char *name)
{
    if (!s || !*s) { fprintf(stderr, "[autoscroll] config: %s missing\n", name); return -1; }
    char *end;
    double val = strtod(s, &end);
    if (*end || end == s) {
        fprintf(stderr, "[autoscroll] config: %s '%s' is not a number\n", name, s);
        return -1;
    }
    if (val < min || val > max) {
        fprintf(stderr, "[autoscroll] config: %s=%g out of range [%g,%g]\n", name, val, min, max);
        return -1;
    }
    *out = val;
    return 0;
}

int config_parse_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;  /* not an error — file may not exist */

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#' || *p == ';') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq++ = '\0';
        p = trim(p);
        eq = trim(eq);

        if      (strcmp(p, "mode") == 0)
            cfg_mode = (strcmp(eq, "position") == 0) ? 1 : 0;
        else if (strcmp(p, "deadzone") == 0) {
            int tmp;
            if (parse_int(eq, &tmp, 0, 9999, "deadzone") == 0)
                cfg_deadzone = tmp;
        } else if (strcmp(p, "gain") == 0) {
            double tmp;
            if (parse_double(eq, &tmp, 0.001, 100.0, "gain") == 0)
                cfg_gain = tmp;
        } else if (strcmp(p, "exponent") == 0) {
            double tmp;
            if (parse_double(eq, &tmp, 0.1, 10.0, "exponent") == 0)
                cfg_exponent = tmp;
        } else if (strcmp(p, "max_speed") == 0) {
            double tmp;
            if (parse_double(eq, &tmp, 0.1, 1000.0, "max_speed") == 0)
                cfg_max_speed = tmp;
        } else if (strcmp(p, "tick_ms") == 0) {
            int tmp;
            if (parse_int(eq, &tmp, 5, 1000, "tick_ms") == 0)
                cfg_tick_ms = tmp;
        } else if (strcmp(p, "position_factor") == 0) {
            double tmp;
            if (parse_double(eq, &tmp, 0.0001, 1.0, "position_factor") == 0)
                cfg_position_factor = tmp;
        } else if (strcmp(p, "horizontal") == 0)
            cfg_horizontal = (strcmp(eq, "false") == 0) ? 0 : 1;
        else if (strcmp(p, "invert_v") == 0)
            cfg_invert_v = (strcmp(eq, "true") == 0) ? 1 : 0;
        else if (strcmp(p, "invert_h") == 0)
            cfg_invert_h = (strcmp(eq, "true") == 0) ? 1 : 0;
        else if (strcmp(p, "match_name") == 0) {
            free(cfg_match_name);
            cfg_match_name = strdup(eq);
        }
    }
    fclose(fp);
    return 0;
}

/* ── CLI arg parser ─────────────────────────────────────────────── */
int config_parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            config_print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            config_print_version();
            exit(0);
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            cfg_foreground = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            cfg_verbose = 1;
        } else if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--list-devices") == 0) {
            cfg_list_devices = 1;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (++i >= argc) { fprintf(stderr, "--mode requires arg\n"); return -1; }
            cfg_mode = (strcmp(argv[i], "position") == 0) ? 1 : 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--deadzone") == 0) {
            if (++i >= argc) { fprintf(stderr, "--deadzone requires arg\n"); return -1; }
            int tmp;
            if (parse_int(argv[i], &tmp, 0, 9999, "--deadzone") != 0) return -1;
            cfg_deadzone = tmp;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gain") == 0) {
            if (++i >= argc) { fprintf(stderr, "--gain requires arg\n"); return -1; }
            double tmp;
            if (parse_double(argv[i], &tmp, 0.001, 100.0, "--gain") != 0) return -1;
            cfg_gain = tmp;
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--exponent") == 0) {
            if (++i >= argc) { fprintf(stderr, "--exponent requires arg\n"); return -1; }
            double tmp;
            if (parse_double(argv[i], &tmp, 0.1, 10.0, "--exponent") != 0) return -1;
            cfg_exponent = tmp;
        } else if (strcmp(argv[i], "--max-speed") == 0) {
            if (++i >= argc) { fprintf(stderr, "--max-speed requires arg\n"); return -1; }
            double tmp;
            if (parse_double(argv[i], &tmp, 0.1, 1000.0, "--max-speed") != 0) return -1;
            cfg_max_speed = tmp;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--position-factor") == 0) {
            if (++i >= argc) { fprintf(stderr, "--position-factor requires arg\n"); return -1; }
            double tmp;
            if (parse_double(argv[i], &tmp, 0.0001, 1.0, "--position-factor") != 0) return -1;
            cfg_position_factor = tmp;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tick") == 0) {
            if (++i >= argc) { fprintf(stderr, "--tick requires arg\n"); return -1; }
            int tmp;
            if (parse_int(argv[i], &tmp, 5, 1000, "--tick") != 0) return -1;
            cfg_tick_ms = tmp;
        } else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--no-horizontal") == 0) {
            cfg_horizontal = 0;
        } else if (strcmp(argv[i], "--invert-v") == 0) {
            cfg_invert_v = 1;
        } else if (strcmp(argv[i], "--invert-h") == 0) {
            cfg_invert_h = 1;
        } else if (strcmp(argv[i], "-D") == 0 || strcmp(argv[i], "--device") == 0) {
            if (++i >= argc) { fprintf(stderr, "--device requires arg\n"); return -1; }
            free(cfg_device_path);
            cfg_device_path = strdup(argv[i]);
        } else if (strcmp(argv[i], "--match-name") == 0) {
            if (++i >= argc) { fprintf(stderr, "--match-name requires arg\n"); return -1; }
            free(cfg_match_name);
            cfg_match_name = strdup(argv[i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

void config_print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n"
           "\n"
           "Scroll behavior:\n"
           "  -m, --mode=rate|position     Scroll feel              [rate]\n"
           "  -d, --deadzone=PX            Deadzone before scroll   [6]\n"
           "  -g, --gain=FLOAT             Rate-mode speed factor   [0.240]\n"
           "  -e, --exponent=FLOAT         Rate-mode curve          [1.00]\n"
           "      --max-speed=FLOAT        Notches/sec cap          [160.0]\n"
           "  -p, --position-factor=FLOAT  Position sensitivity     [0.015]\n"
           "  -t, --tick=MS                Rate-mode tick           [15]\n"
           "  -H, --no-horizontal          Disable horizontal scrolling\n"
           "      --invert-v               Flip vertical scroll\n"
           "      --invert-h               Flip horizontal scroll\n"
           "\n"
           "Device selection:\n"
           "  -L, --list-devices           List input devices\n"
           "  -D, --device=PATH            Pin specific /dev/input/eventN\n"
           "      --match-name=REGEX       Device name filter regex    [(none)]\n"
           "\n"
           "Runtime:\n"
           "  -f, --foreground             Don't daemonize\n"
           "  -v, --verbose                Log state transitions\n"
           "  -V, --version\n"
           "  -h, --help\n",
           prog);
}

void config_print_version(void)
{
    printf("autoscroll version %s\n", "1.0.0");
}

int config_list_devices(void)
{
    glob_t g;
    int ret = glob("/dev/input/event*", GLOB_NOSORT, NULL, &g);
    if (ret != 0) {
        fprintf(stderr, "No input devices found.\n");
        return 1;
    }
    printf("Available input devices:\n");
    for (size_t i = 0; i < g.gl_pathc; i++) {
        int fd = open(g.gl_pathv[i], O_RDONLY);
        if (fd < 0) continue;
        char name[128] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0)
            printf("  %-20s  %s\n", g.gl_pathv[i], name);
        close(fd);
    }
    globfree(&g);
    return 0;
}
