#ifndef EVDEV_BACKEND_H
#define EVDEV_BACKEND_H

#include "scroll_engine.h"

/* ── Device handles ──────────────────────────────────────────────── */
struct Device {
    int    fd;                    /* evdev fd (grabbed) */
    char   path[256];             /* /dev/input/eventN */
    char   name[128];             /* human-readable name */
    struct ScrollEngine engine;   /* per-device state machine */
    struct Device *next;          /* linked list */
};

/* ── Uinput handle (single shared output) ───────────────────────── */
struct UinputDev {
    int fd;
};

/* ── Device discovery ──────────────────────────────────────────────
 *   Scans /dev/input/ for event devices matching cfg_match_name.
 *   Returns linked list. Skips touchpads, tablets, virtual devices.
 */
struct Device *evdev_discover(const char *match_regex);

/* ── Grab a single device ──────────────────────────────────────────
 *   Attempts EVIOCGRAB. Returns 0 on success, -1 on failure.
 */
int evdev_grab(struct Device *dev);

/* ── Create uinput device ──────────────────────────────────────────
 *   Creates a single virtual output device for injection.
 *   Returns 0 on success, -1 on failure.
 */
int evdev_create_uinput(struct UinputDev *u);

/* ── Event forwarding ──────────────────────────────────────────────
 *   Forward a single event to the uinput device.
 */
int evdev_forward(const struct UinputDev *u, const struct input_event *ev);

/* ── Emit scroll events ────────────────────────────────────────────
 *   Emits hi-res + low-res wheel events through uinput.
 */
int evdev_emit_scroll(const struct UinputDev *u, int v_notches, int h_notches);

/* ── Synthesize a single middle-click ──────────────────────────────
 *   Sends BTN_MIDDLE down + up through uinput (for paste passthrough).
 */
int evdev_synthesize_click(const struct UinputDev *u);

/* ── Emit scroll as raw 120ths (hi-res) + whole notches (low-res) ── */
int evdev_emit_scroll_120(const struct UinputDev *u, int v_120, int h_120);

/* ── Cleanup ─────────────────────────────────────────────────────── */
void evdev_destroy_uinput(struct UinputDev *u);
void evdev_free_devices(struct Device *list);

#endif /* EVDEV_BACKEND_H */
