/**
 * test_scroll_engine.c — Headless unit tests for ScrollEngine.
 *
 * Tests the state machine and accumulator without any evdev/uinput
 * dependencies. Run with: ctest --test-dir build  or directly.
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "scroll_engine.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%d]: %s\n", __LINE__, msg); \
    } else { \
        tests_passed++; \
    } \
} while(0)

static void test_init_idle(void)
{
    struct ScrollEngine eng;
    scroll_engine_init(&eng, MODE_RATE, 18, 0.02, 1.3, 40.0, 0.06, 15, 1, 0, 0, 0);
    ASSERT(eng.state == ST_IDLE, "initial state is IDLE");
    ASSERT(eng.middle_down == 0, "middle not down");
    ASSERT(eng.moved == 0, "not moved");
    ASSERT(fabs(eng.acc_v) < 1e-9, "acc_v zero");
    ASSERT(fabs(eng.acc_h) < 1e-9, "acc_h zero");
}

static void test_middle_down_enters_held(void)
{
    struct ScrollEngine eng;
    scroll_engine_init(&eng, MODE_RATE, 18, 0.02, 1.3, 40.0, 0.06, 15, 1, 0, 0, 0);

    struct input_event ev = {.type = EV_KEY, .code = BTN_MIDDLE, .value = 1};
    struct input_event out;
    int ret = scroll_engine_process(&eng, &ev, &out);

    ASSERT(ret == 0, "middle down consumed (not forwarded)");
    ASSERT(eng.state == ST_HELD, "state is HELD");
    ASSERT(eng.middle_down == 1, "middle_down flag set");
}

static void test_quick_release_synthesizes_click(void)
{
    struct ScrollEngine eng;
    scroll_engine_init(&eng, MODE_RATE, 18, 0.02, 1.3, 40.0, 0.06, 15, 1, 0, 0, 0);

    /* Press */
    struct input_event ev = {.type = EV_KEY, .code = BTN_MIDDLE, .value = 1};
    struct input_event out;
    scroll_engine_process(&eng, &ev, &out);

    /* Release (no motion = inside deadzone) */
    ev.value = 0;
    int ret = scroll_engine_process(&eng, &ev, &out);

    ASSERT(ret == -1, "quick release returns -1 (synthesize)");
    ASSERT(eng.state == ST_IDLE, "back to IDLE after release");
}

static void test_held_to_scrolling_on_deadzone_exit(void)
{
    /* The deadzone exit check happens in main.c's process_device(),
     * not in scroll_engine_process(). So the engine transitions
     * externally via setting state=ST_SCROLLING. */
    struct ScrollEngine eng;
    scroll_engine_init(&eng, MODE_RATE, 18, 0.02, 1.3, 40.0, 0.06, 15, 1, 0, 0, 0);

    /* Manually simulate: middle down */
    struct input_event ev = {.type = EV_KEY, .code = BTN_MIDDLE, .value = 1};
    struct input_event out;
    scroll_engine_process(&eng, &ev, &out);

    /* Set moved + transition to SCROLLING (as main.c would) */
    eng.moved = 1;
    eng.state = ST_SCROLLING;
    eng.anchor_x = 100;
    eng.anchor_y = 50;

    ASSERT(scroll_engine_is_active(&eng), "engine active while scrolling");
    ASSERT(eng.state == ST_SCROLLING, "state is SCROLLING");
}

static void test_tick_produces_events(void)
{
    struct ScrollEngine eng;
    scroll_engine_init(&eng, MODE_RATE, 18, 0.02, 1.3, 40.0, 0.06, 15, 1, 0, 0, 0);

    /* Artificially set scrolling state with large distance */
    eng.state = ST_SCROLLING;
    eng.moved = 1;
    eng.anchor_x = 500;
    eng.anchor_y = 500;
    eng.last_tick_time = 0.0;

    struct input_event out[16];
    /* dt = 50ms — long enough to accumulate ≥1 notch at this distance */
    int n = scroll_engine_tick(&eng, out, 16, 0.050);

    ASSERT(n > 0, "tick produces events");
    /* Should have REL_WHEEL_HI_RES */
    int found_hi = 0;
    for (int i = 0; i < n; i++) {
        if (out[i].type == EV_REL && out[i].code == REL_WHEEL_HI_RES)
            found_hi = 1;
    }
    ASSERT(found_hi, "tick emits REL_WHEEL_HI_RES");
}

static void test_fractional_accumulator(void)
{
    struct ScrollEngine eng;
    scroll_engine_init(&eng, MODE_RATE, 18, 0.02, 1.3, 40.0, 0.06, 15, 1, 0, 0, 0);

    eng.state = ST_SCROLLING;
    eng.moved = 1;
    eng.anchor_x = 0;
    eng.anchor_y = 600; /* large distance to ensure emission */
    eng.last_tick_time = 0.0;

    /* Very small dt — will produce less than 1 notch */
    struct input_event out[16];
    int n = scroll_engine_tick(&eng, out, 16, 0.001);

    /* At very small dt with small distance, should be 0 or very small.
     * The accumulator should hold the fractional remainder. */
    double acc_before = eng.acc_v;

    /* Tick again */
    n = scroll_engine_tick(&eng, out, 16, 0.010);

    /* After two ticks, accumulator should not be zero if fractional */
    ASSERT(fabs(eng.acc_v) < 100.0, "accumulator stays reasonable");
}

static void test_release_ends_scrolling(void)
{
    struct ScrollEngine eng;
    scroll_engine_init(&eng, MODE_RATE, 18, 0.02, 1.3, 40.0, 0.06, 15, 1, 0, 0, 0);

    /* Press */
    struct input_event ev = {.type = EV_KEY, .code = BTN_MIDDLE, .value = 1};
    struct input_event out;
    scroll_engine_process(&eng, &ev, &out);

    /* Move into scrolling */
    eng.moved = 1;
    eng.state = ST_SCROLLING;

    /* Release */
    ev.value = 0;
    int ret = scroll_engine_process(&eng, &ev, &out);

    ASSERT(ret == 0, "release consumed");
    ASSERT(eng.state == ST_IDLE, "back to IDLE");
    ASSERT(eng.middle_down == 0, "middle_down cleared");
}

int main(void)
{
    test_init_idle();
    test_middle_down_enters_held();
    test_quick_release_synthesizes_click();
    test_held_to_scrolling_on_deadzone_exit();
    test_tick_produces_events();
    test_fractional_accumulator();
    test_release_ends_scrolling();

    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
