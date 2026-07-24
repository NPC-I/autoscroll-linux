#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <regex.h>
#include <libgen.h>
#include <strings.h>
#include <time.h>

#include "evdev_backend.h"
#include "config.h"

/* ── Helpers ──────────────────────────────────────────────────────── */
static int get_device_name(int fd, char *buf, size_t size)
{
    if (ioctl(fd, EVIOCGNAME((int)size - 1), buf) < 0)
        return -1;
    buf[size - 1] = '\0';
    return 0;
}

static int has_capability(int fd, int ev_type, int code)
{
    unsigned long bits[256 / sizeof(unsigned long)] = {0};
    if (ioctl(fd, EVIOCGBIT(ev_type, sizeof(bits)), bits) < 0)
        return 0;
    return (bits[code / (sizeof(unsigned long) * 8)] >> (code % (sizeof(unsigned long) * 8))) & 1;
}

/* ── strcasestr (portable, forward declaration) ──────────────────── */
static char *my_strcasestr(const char *haystack, const char *needle);

/* ── Exclusion checks ────────────────────────────────────────────── */
static int is_touchpad(int fd, const char *name)
{
    if (has_capability(fd, EV_KEY, BTN_TOOL_FINGER)) return 1;
    if (has_capability(fd, EV_ABS, ABS_MT_POSITION_X)) return 1;
    const char *touch_keywords[] = {
        "touchpad", "synaptics", "alps", "focaltech",
        "cypress", "elan", "etps/2", "trackpad", NULL
    };
    for (int i = 0; touch_keywords[i]; i++) {
        if (strstr(name, touch_keywords[i]) != NULL ||
            my_strcasestr(name, touch_keywords[i]) != NULL)
            return 1;
    }
    return 0;
}

static int is_tablet(int fd, const char *name)
{
    if (has_capability(fd, EV_KEY, BTN_STYLUS)) return 1;
    if (has_capability(fd, EV_KEY, BTN_TOOL_PEN)) return 1;
    const char *tablet_keywords[] = {
        "wacom", "pentablet", "penpad", "pen", "digitizer", NULL
    };
    for (int i = 0; tablet_keywords[i]; i++) {
        if (strstr(name, tablet_keywords[i]) != NULL ||
            my_strcasestr(name, tablet_keywords[i]) != NULL)
            return 1;
    }
    return 0;
}

static int is_virtual(const char *name)
{
    return (strstr(name, "virtual") != NULL ||
            strstr(name, "uinput") != NULL ||
            strstr(name, "autoscroll") != NULL) ? 1 : 0;
}

/* ── strcasestr (portable) ───────────────────────────────────────── */
static char *my_strcasestr(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncasecmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
        haystack++;
    }
    return NULL;
}

/* ── Device discovery ────────────────────────────────────────────── */
struct Device *evdev_discover(const char *match_regex)
{
    regex_t re;
    int use_regex = 0;
    if (match_regex && *match_regex) {
        if (regcomp(&re, match_regex, REG_EXTENDED | REG_NOSUB | REG_ICASE) != 0) {
            fprintf(stderr, "Invalid regex: %s\n", match_regex);
            return NULL;
        }
        use_regex = 1;
    }

    struct Device *list = NULL, **tail = &list;
    glob_t g;
    int ret = glob("/dev/input/event*", GLOB_NOSORT, NULL, &g);
    if (ret != 0) {
        if (use_regex) regfree(&re);
        return NULL;
    }

    for (size_t i = 0; i < g.gl_pathc; i++) {
        int fd = open(g.gl_pathv[i], O_RDWR);
        if (fd < 0) continue;

        char name[128] = {0};
        if (get_device_name(fd, name, sizeof(name)) < 0) {
            close(fd);
            continue;
        }

        /* Exclusion checks */
        if (is_touchpad(fd, name) || is_tablet(fd, name) || is_virtual(name)) {
            close(fd);
            continue;
        }

        /* Must be a mouse (has BTN_LEFT or BTN_MOUSE) */
        if (!has_capability(fd, EV_KEY, BTN_LEFT) &&
            !has_capability(fd, EV_KEY, BTN_MOUSE)) {
            close(fd);
            continue;
        }

        /* Regex name match */
        if (use_regex && regexec(&re, name, 0, NULL, 0) != 0) {
            close(fd);
            continue;
        }

        /* Create device node */
        struct Device *dev = calloc(1, sizeof(*dev));
        if (!dev) { close(fd); continue; }
        dev->fd = fd;
        snprintf(dev->path, sizeof(dev->path), "%s", g.gl_pathv[i]);
        snprintf(dev->name, sizeof(dev->name), "%s", name);
        dev->next = NULL;

        /* Initialize scroll engine */
        scroll_engine_init(&dev->engine,
                           (enum scroll_mode)cfg_mode,
                           cfg_deadzone, cfg_gain, cfg_exponent,
                           cfg_max_speed, cfg_position_factor,
                           cfg_tick_ms, cfg_horizontal,
                           cfg_invert_v, cfg_invert_h,
                           cfg_verbose);

        *tail = dev;
        tail = &dev->next;

        if (cfg_verbose)
            fprintf(stderr, "[autoscroll] Found: %s  (%s)\n", dev->path, dev->name);
    }

    globfree(&g);
    if (use_regex) regfree(&re);
    return list;
}

int evdev_grab(struct Device *dev)
{
    if (ioctl(dev->fd, EVIOCGRAB, (void *)1) < 0) {
        switch (errno) {
        case EBUSY:
            fprintf(stderr, "[autoscroll] Device %s is grabbed by another app\n", dev->path);
            break;
        case EACCES:
            fprintf(stderr, "[autoscroll] Permission denied on %s (need root?)\n", dev->path);
            break;
        case ENODEV:
        case ENXIO:
            fprintf(stderr, "[autoscroll] Device %s disconnected\n", dev->path);
            break;
        default:
            fprintf(stderr, "[autoscroll] Failed to grab %s: %s\n", dev->path, strerror(errno));
        }
        return -1;
    }
    return 0;
}

/* ── Uinput creation ─────────────────────────────────────────────── */
int evdev_create_uinput(struct UinputDev *u)
{
    u->fd = open("/dev/uinput", O_RDWR);
    if (u->fd < 0) {
        fprintf(stderr, "Cannot open /dev/uinput (need root?): %s\n", strerror(errno));
        return -1;
    }

    /* Enable event types */
    ioctl(u->fd, UI_SET_EVBIT, EV_KEY);
    ioctl(u->fd, UI_SET_EVBIT, EV_REL);
    ioctl(u->fd, UI_SET_EVBIT, EV_SYN);

    /* Key: BTN_MIDDLE for paste passthrough */
    ioctl(u->fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(u->fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(u->fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(u->fd, UI_SET_KEYBIT, BTN_SIDE);
    ioctl(u->fd, UI_SET_KEYBIT, BTN_EXTRA);

    /* Relative axes */
    ioctl(u->fd, UI_SET_RELBIT, REL_X);
    ioctl(u->fd, UI_SET_RELBIT, REL_Y);
    ioctl(u->fd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(u->fd, UI_SET_RELBIT, REL_HWHEEL);
    ioctl(u->fd, UI_SET_RELBIT, REL_WHEEL_HI_RES);
    ioctl(u->fd, UI_SET_RELBIT, REL_HWHEEL_HI_RES);

    struct uinput_setup usetup = {
        .id = {
            .bustype = BUS_VIRTUAL,
            .vendor  = 0x1234,
            .product = 0x5678,
            .version = 0,
        },
        .name = "autoscroll-virtual-device",
    };
    if (ioctl(u->fd, UI_DEV_SETUP, &usetup) < 0) {
        fprintf(stderr, "UI_DEV_SETUP failed: %s\n", strerror(errno));
        close(u->fd);
        return -1;
    }
    if (ioctl(u->fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "UI_DEV_CREATE failed: %s\n", strerror(errno));
        close(u->fd);
        return -1;
    }

    /* Brief delay for udev to recognize */
    {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000000L };
        nanosleep(&ts, NULL);
    }

    return 0;
}

/* ── Event forwarding ────────────────────────────────────────────── */
int evdev_forward(const struct UinputDev *u, const struct input_event *ev)
{
    if (write(u->fd, ev, sizeof(*ev)) < 0)
        return -1;
    return 0;
}

int evdev_emit_scroll(const struct UinputDev *u, int v_notches, int h_notches)
{
    struct input_event ev[8];
    int n = 0;

    if (v_notches) {
        ev[n++] = (struct input_event){.type = EV_REL, .code = REL_WHEEL_HI_RES,
                                       .value = v_notches * 120};
        ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
    }
    if (h_notches) {
        ev[n++] = (struct input_event){.type = EV_REL, .code = REL_HWHEEL_HI_RES,
                                       .value = h_notches * 120};
        ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
    }
    if (v_notches) {
        ev[n++] = (struct input_event){.type = EV_REL, .code = REL_WHEEL,
                                       .value = v_notches};
        ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
    }
    if (h_notches) {
        ev[n++] = (struct input_event){.type = EV_REL, .code = REL_HWHEEL,
                                       .value = h_notches};
        ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
    }
    if (write(u->fd, ev, n * sizeof(struct input_event)) < 0)
        return -1;
    return 0;
}

int evdev_synthesize_click(const struct UinputDev *u)
{
    struct input_event ev[] = {
        {.type = EV_KEY, .code = BTN_MIDDLE, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT},
        {.type = EV_KEY, .code = BTN_MIDDLE, .value = 0},
        {.type = EV_SYN, .code = SYN_REPORT},
    };
    return write(u->fd, ev, sizeof(ev)) < 0 ? -1 : 0;
}

int evdev_emit_scroll_120(const struct UinputDev *u, int v_120, int h_120)
{
    struct input_event ev[8];
    int n = 0;

    if (v_120) {
        ev[n++] = (struct input_event){.type = EV_REL, .code = REL_WHEEL_HI_RES,
                                       .value = v_120};
        ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
        int v_notches = v_120 / 120;
        if (v_notches) {
            ev[n++] = (struct input_event){.type = EV_REL, .code = REL_WHEEL,
                                           .value = v_notches};
            ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
        }
    }
    if (h_120) {
        ev[n++] = (struct input_event){.type = EV_REL, .code = REL_HWHEEL_HI_RES,
                                       .value = h_120};
        ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
        int h_notches = h_120 / 120;
        if (h_notches) {
            ev[n++] = (struct input_event){.type = EV_REL, .code = REL_HWHEEL,
                                           .value = h_notches};
            ev[n++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
        }
    }
    if (write(u->fd, ev, n * sizeof(struct input_event)) < 0)
        return -1;
    return 0;
}

/* ── Cleanup ──────────────────────────────────────────────────────── */
void evdev_destroy_uinput(struct UinputDev *u)
{
    if (u->fd >= 0) {
        ioctl(u->fd, UI_DEV_DESTROY);
        close(u->fd);
        u->fd = -1;
    }
}

void evdev_free_devices(struct Device *list)
{
    while (list) {
        struct Device *next = list->next;
        if (list->fd >= 0) {
            ioctl(list->fd, EVIOCGRAB, (void *)0); /* ungrab */
            close(list->fd);
        }
        free(list);
        list = next;
    }
}
