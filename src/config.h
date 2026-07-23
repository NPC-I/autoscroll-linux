#ifndef CONFIG_H
#define CONFIG_H

/* ── Scroll behavior ────────────────────────────────────────────── */
extern int          cfg_mode;           /* 0=rate, 1=position */
extern int          cfg_deadzone;       /* px before scroll starts */
extern double       cfg_gain;           /* rate-mode speed factor */
extern double       cfg_exponent;       /* rate-mode curve exponent */
extern double       cfg_max_speed;      /* notches/sec cap */
extern double       cfg_position_factor;/* position-mode sensitivity */
extern int          cfg_tick_ms;        /* rate-mode tick interval */
extern int          cfg_horizontal;     /* allow horizontal scrolling */
extern int          cfg_invert_v;       /* flip vertical dir */
extern int          cfg_invert_h;       /* flip horizontal dir */

/* ── Device selection ───────────────────────────────────────────── */
extern char        *cfg_match_name;     /* regex for device name match */
extern char        *cfg_device_path;    /* pinned /dev/input/eventN */

/* ── Runtime ────────────────────────────────────────────────────── */
extern int          cfg_foreground;     /* don't daemonize */
extern int          cfg_verbose;        /* log state transitions */
extern int          cfg_list_devices;   /* list and exit */

/* ── Config file path ───────────────────────────────────────────── */
#define CONFIG_PATH "/etc/autoscroll.conf"

/* ── Defaults ───────────────────────────────────────────────────── */
#define DEFAULT_MODE            0       /* rate (rate=0, position=1) */
#define DEFAULT_DEADZONE        6
#define DEFAULT_GAIN            0.240
#define DEFAULT_EXPONENT        1.00
#define DEFAULT_MAX_SPEED       160.0
#define DEFAULT_POSITION_FACTOR 0.015
#define DEFAULT_TICK_MS         15
#define DEFAULT_HORIZONTAL      1
#define DEFAULT_INVERT_V        0
#define DEFAULT_INVERT_H        0
#define DEFAULT_MATCH_NAME      ""

/* ── Functions ──────────────────────────────────────────────────── */
void config_set_defaults(void);
int  config_parse_file(const char *path);
int  config_parse_args(int argc, char **argv);
void config_print_usage(const char *prog);
void config_print_version(void);
int  config_list_devices(void);

#endif /* CONFIG_H */
