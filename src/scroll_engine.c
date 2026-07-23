#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "scroll_engine.h"

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
                        int verbose)
{
    eng->mode            = mode;
    eng->deadzone        = deadzone;
    eng->gain            = gain;
    eng->exponent        = exponent;
    eng->max_speed       = max_speed;
    eng->position_factor = position_factor;
    eng->tick_ms         = tick_ms;
    eng->horizontal      = horizontal;
    eng->invert_v        = invert_v;
    eng->invert_h        = invert_h;
    eng->verbose         = verbose;

    eng->state       = ST_IDLE;
    eng->anchor_x    = 0;
    eng->anchor_y    = 0;
    eng->middle_down = 0;
    eng->moved       = 0;
    eng->acc_v       = 0.0;
    eng->acc_h       = 0.0;
    eng->last_tick_time = 0.0;
}

void scroll_engine_reset(struct ScrollEngine *eng)
{
    eng->state    = ST_IDLE;
    eng->moved    = 0;
    eng->acc_v    = 0.0;
    eng->acc_h    = 0.0;
}

int scroll_engine_process(struct ScrollEngine *eng,
                          const struct input_event *ev,
                          struct input_event *out_ev)
{
    if (ev->type == EV_KEY && ev->code == BTN_MIDDLE) {
        if (ev->value == 1) {
            /* Middle button pressed */
            eng->middle_down = 1;
            if (eng->mode == MODE_POSITION) {
                /* Position mode: scroll immediately from any motion,
                 * no deadzone to cross. Enter SCROLLING right away. */
                eng->moved = 1;
                eng->state = ST_SCROLLING;
                eng->last_tick_time = 0.0;
                eng->acc_v = 0.0;
                eng->acc_h = 0.0;
                if (eng->verbose)
                    fprintf(stderr, "[autoscroll] SCROLLING (position mode)\n");
            } else {
                eng->state = ST_HELD;
                if (eng->verbose)
                    fprintf(stderr, "[autoscroll] HELD (anchor pending)\n");
            }
            return 0; /* consume — don't forward yet */
        } else if (ev->value == 0) {
            /* Middle button released */
            eng->middle_down = 0;
            if (eng->state == ST_HELD && !eng->moved) {
                /* Quick click inside deadzone → synthesize middle-click */
                if (eng->verbose)
                    fprintf(stderr, "[autoscroll] quick release → paste passthrough\n");
                /* Return -1 to signal caller to synthesize the click */
                scroll_engine_reset(eng);
                return -1;
            }
            scroll_engine_reset(eng);
            if (eng->verbose)
                fprintf(stderr, "[autoscroll] IDLE\n");
            return 0; /* consumed */
        }
        return 0;
    }

    /* ── Track pointer motion to set anchor / detect deadzone exit ── */
    if (ev->type == EV_REL) {
        if (ev->code == REL_X || ev->code == REL_Y) {
            if (eng->state == ST_HELD) {
                /* Update anchor on first motion */
                if (!eng->moved) {
                    eng->anchor_x = 0; /* relative — we track delta */
                    eng->anchor_y = 0;
                    /* Need absolute anchor set by caller */
                }
                /* Delegate deadzone check to caller via state tracking */
            }
            /* Always forward REL events except during HELD or SCROLLING */
            if (eng->state != ST_IDLE) {
                return 0; /* suppress pointer motion while autoscrolling */
            }
        }
        return 1; /* forward */
    }

    /* Forward all other events unless we're in an active state */
    if (eng->state != ST_IDLE) {
        return 0; /* suppress */
    }
    return 1; /* forward */
}

int scroll_engine_tick(struct ScrollEngine *eng,
                       struct input_event *out,
                       int out_max,
                       double now)
{
    if (eng->state != ST_SCROLLING)
        return 0;
    if (!eng->moved)
        return 0;

    double dt = now - eng->last_tick_time;
    if (dt <= 0.0) dt = (double)eng->tick_ms / 1000.0;
    eng->last_tick_time = now;

    /* Compute velocity in notches/sec */
    double dist_v = fabs((double)eng->anchor_y);
    double dist_h = fabs((double)eng->anchor_x);
    int sign_v = (eng->anchor_y >= 0) ? -1 : 1;
    int sign_h = (eng->anchor_x >= 0) ? -1 : 1;

    if (eng->invert_v) sign_v = -sign_v;
    if (eng->invert_h) sign_h = -sign_h;

    double vel_v = 0.0, vel_h = 0.0;
    int n = 0;

    if (dist_v > eng->deadzone) {
        double d = dist_v - eng->deadzone;
        vel_v = eng->gain * pow(d, eng->exponent);
        if (vel_v > eng->max_speed) vel_v = eng->max_speed;
        /* Floor: minimum 1 notch/sec so slow pulls emit a steady
         * stream of fractional 120th events instead of silence. */
        if (vel_v < 1.0) vel_v = 1.0;
    }
    if (eng->horizontal && dist_h > eng->deadzone) {
        double d = dist_h - eng->deadzone;
        vel_h = eng->gain * pow(d, eng->exponent);
        if (vel_h > eng->max_speed) vel_h = eng->max_speed;
        if (vel_h < 1.0) vel_h = 1.0;
    }

    /* Accumulate fractional notches */
    eng->acc_v += vel_v * dt * sign_v;
    eng->acc_h += vel_h * dt * sign_h;

    /* Emit hi-res (120 units/notch) on every tick.
     * Force minimum 1 unit so there's always a steady stream
     * of events — the compositor accumulates these internally
     * and produces smooth scroll output. Without this floor,
     * ticks with 0 emitted create visible gaps (microjitter). */
    int emit_120_v = (int)(eng->acc_v * 120.0);
    int emit_120_h = (int)(eng->acc_h * 120.0);

    /* Guarantee at least 1 unit emitted per tick when active */
    if (emit_120_v == 0) {
        double raw_v = eng->acc_v * 120.0;
        if (raw_v > 1e-9)      emit_120_v = 1;
        else if (raw_v < -1e-9) emit_120_v = -1;
    }
    if (emit_120_h == 0) {
        double raw_h = eng->acc_h * 120.0;
        if (raw_h > 1e-9)      emit_120_h = 1;
        else if (raw_h < -1e-9) emit_120_h = -1;
    }

    /* scroll_grain divides emitted value for finer page movement.
     * grain=4 means each event scrolls 1/4 standard notch worth. */
    int scroll_grain = 4;
    emit_120_v /= scroll_grain;
    emit_120_h /= scroll_grain;

    eng->acc_v -= (double)(emit_120_v * scroll_grain) / 120.0;
    eng->acc_h -= (double)(emit_120_h * scroll_grain) / 120.0;

    if (emit_120_v == 0 && emit_120_h == 0)
        return 0;

    /* Emit hi-res first, then low-res for compat */
    int idx = 0;
    if (idx + 4 > out_max) return idx;

    if (emit_120_v) {
        out[idx++] = (struct input_event){.type = EV_REL, .code = REL_WHEEL_HI_RES,
                                          .value = emit_120_v};
        out[idx++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT, .value = 0};
    }
    if (emit_120_h) {
        out[idx++] = (struct input_event){.type = EV_REL, .code = REL_HWHEEL_HI_RES,
                                          .value = emit_120_h};
        out[idx++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT, .value = 0};
    }
    /* Low-res: only emit whole notches that have been sent via hi-res.
     * Integer division truncates toward zero, matching 120-unit grouping. */
    int emit_v = emit_120_v / 120;
    int emit_h = emit_120_h / 120;
    if (emit_v) {
        out[idx++] = (struct input_event){.type = EV_REL, .code = REL_WHEEL,
                                          .value = emit_v};
        out[idx++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT, .value = 0};
    }
    if (emit_h) {
        out[idx++] = (struct input_event){.type = EV_REL, .code = REL_HWHEEL,
                                          .value = emit_h};
        out[idx++] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT, .value = 0};
    }

    return idx;
}

int scroll_engine_is_active(const struct ScrollEngine *eng)
{
    return eng->state != ST_IDLE;
}

const char *scroll_engine_state_name(const struct ScrollEngine *eng)
{
    switch (eng->state) {
    case ST_IDLE:      return "IDLE";
    case ST_HELD:      return "HELD";
    case ST_SCROLLING: return "SCROLLING";
    default:           return "?";
    }
}
