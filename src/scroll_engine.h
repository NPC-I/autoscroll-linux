#ifndef SCROLL_ENGINE_H
#define SCROLL_ENGINE_H

#include <linux/input.h>   /* struct input_event, BTN_*, REL_* */

/* ── State machine ──────────────────────────────────────────────── */
enum scroll_state {
    ST_IDLE      = 0,
    ST_HELD      = 1,      /* middle button down, inside deadzone */
    ST_SCROLLING = 2,      /* outside deadzone, actively scrolling */
};

/* ── Mode ────────────────────────────────────────────────────────── */
enum scroll_mode {
    MODE_RATE     = 0,
    MODE_POSITION = 1,
};

/* ── Engine instance (one per physical device) ──────────────────── */
struct ScrollEngine {
    /* ── Configuration (copied at init) ── */
    enum scroll_mode  mode;
    int               deadzone;
    double            gain;
    double            exponent;
    double            max_speed;
    double            position_factor;
    int               tick_ms;
    int               horizontal;
    int               invert_v;
    int               invert_h;
    int               verbose;

    /* ── State ── */
    enum scroll_state state;
    int               anchor_x;        /* cursor pos when middle pressed */
    int               anchor_y;
    int               middle_down;     /* BTN_MIDDLE currently held */
    int               moved;           /* pointer left deadzone */

    /* ── Accumulators ── */
    double            acc_v;           /* fractional vertical notches */
    double            acc_h;           /* fractional horizontal notches */

    /* ── Last tick time for rate mode ── */
    double            last_tick_time;  /* seconds (monotonic) */
};

/* ── Lifecycle ──────────────────────────────────────────────────── */
void scroll_engine_init(struct ScrollEngine *eng,
                        enum scroll_mode mode,
                        int deadzone,
                        double gain,
                        double exponent,
                        double max_speed,
                        double position_factor,
                        int tick_ms,
                        int horizontal,
                        int invert_v,
                        int invert_h,
                        int verbose);

void scroll_engine_reset(struct ScrollEngine *eng);

/* ── Event processing ──────────────────────────────────────────────
 *   process_event() returns:
 *     0 → event was consumed (suppressed/absorbed)
 *     1 → event should be forwarded to uinput unchanged
 *    -1 → event was synthesized (caller should forward the event
 *         in the output parameter)
 */
int scroll_engine_process(struct ScrollEngine *eng,
                          const struct input_event *ev,
                          struct input_event *out_ev);

/* ── Tick for rate mode ────────────────────────────────────────────
 *   Returns number of events written to out (max 4: V_HI_RES, H_HI_RES,
 *   V, H, each followed by SYN_REPORT, or 2 combined batches).
 *   Returns 0 when nothing to emit.
 */
int scroll_engine_tick(struct ScrollEngine *eng,
                       struct input_event *out,
                       int out_max,
                       double now);

/* ── Query ──────────────────────────────────────────────────────── */
int scroll_engine_is_active(const struct ScrollEngine *eng);
const char *scroll_engine_state_name(const struct ScrollEngine *eng);

#endif /* SCROLL_ENGINE_H */
