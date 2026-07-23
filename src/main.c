#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <sys/signalfd.h>
#include <sys/stat.h>

#include "config.h"
#include "scroll_engine.h"
#include "evdev_backend.h"

/* ── Globals (for signal handler cleanup) ────────────────────────── */
static struct UinputDev  g_uinput = { -1 };
static struct Device    *g_devices = NULL;
static volatile int      g_running = 1;

static void cleanup_and_exit(int code)
{
    g_running = 0;
    evdev_destroy_uinput(&g_uinput);
    evdev_free_devices(g_devices);
    g_devices = NULL;
    exit(code);
}

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ── Monotonic time helper ───────────────────────────────────────── */
static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ── Debug event logger ──────────────────────────────────────────── */
static void log_event(const struct input_event *ev, const char *prefix, const char *devname)
{
    if (!cfg_verbose) return;
    if (ev->type == EV_KEY)
        fprintf(stderr, "[autoscroll] %s [%s] KEY code=%d val=%d\n",
                prefix, devname, ev->code, ev->value);
    else if (ev->type == EV_REL)
        fprintf(stderr, "[autoscroll] %s [%s] REL code=%d val=%d\n",
                prefix, devname, ev->code, ev->value);
}

/* ── Process events from one device ──────────────────────────────── */
static int process_device(struct Device *dev, const struct UinputDev *u)
{
    struct input_event buf[64];
    int n = (int)read(dev->fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN) return 0;
        /* Device disconnected */
        fprintf(stderr, "[autoscroll] Device %s removed: %s\n",
                dev->path, strerror(errno));
        return -1;
    }
    if (n == 0) return -1; /* EOF */

    int count = n / (int)sizeof(struct input_event);

    for (int i = 0; i < count; i++) {
        struct input_event out_ev = buf[i];

        log_event(&buf[i], "read", dev->name);

        /* ── Track pointer position for deadzone detection ── */
        if (buf[i].type == EV_REL) {
            struct ScrollEngine *eng = &dev->engine;

            if (eng->state == ST_HELD && !eng->moved) {
                if (buf[i].code == REL_X) eng->anchor_x += buf[i].value;
                if (buf[i].code == REL_Y) eng->anchor_y += buf[i].value;

                /* Check if we've left the deadzone */
                int dx = abs(eng->anchor_x);
                int dy = abs(eng->anchor_y);
                int dist = (dx > dy) ? dx : dy;  /* Chebyshev distance */

                if (dist > eng->deadzone) {
                    eng->moved = 1;
                    eng->state = ST_SCROLLING;
                    eng->last_tick_time = now_seconds();
                    eng->acc_v = 0.0;
                    eng->acc_h = 0.0;
                    if (eng->verbose)
                        fprintf(stderr, "[autoscroll] SCROLLING (dist=%d)\n", dist);
                }
            }

            /* Continue accumulating anchor while scrolling so speed
             * increases as the user moves the mouse further from
             * the original anchor point. */
            if (eng->state == ST_SCROLLING) {
                if (buf[i].code == REL_X) eng->anchor_x += buf[i].value;
                if (buf[i].code == REL_Y) eng->anchor_y += buf[i].value;
            }

            /* Accumulate position-mode motion */
            if (eng->state == ST_SCROLLING && eng->mode == MODE_POSITION) {
                int dx = buf[i].code == REL_X ? buf[i].value : 0;
                int dy = buf[i].code == REL_Y ? buf[i].value : 0;
                if (eng->invert_v) dy = -dy;
                if (eng->invert_h) dx = -dx;

                eng->acc_v += -(double)dy * eng->position_factor;
                eng->acc_h += (double)dx * eng->position_factor;

                /* Emit hi-res 120ths immediately for smooth scroll.
                 * Force minimum 1 unit per event for steady stream. */
                int emit_120_v = (int)(eng->acc_v * 120.0);
                int emit_120_h = (int)(eng->acc_h * 120.0);
                if (emit_120_v == 0) {
                    double r = eng->acc_v * 120.0;
                    if (r > 1e-9) emit_120_v = 1; else if (r < -1e-9) emit_120_v = -1;
                }
                if (emit_120_h == 0) {
                    double r = eng->acc_h * 120.0;
                    if (r > 1e-9) emit_120_h = 1; else if (r < -1e-9) emit_120_h = -1;
                }
                int scroll_grain = 4;
                emit_120_v /= scroll_grain;
                emit_120_h /= scroll_grain;
                eng->acc_v -= (double)(emit_120_v * scroll_grain) / 120.0;
                eng->acc_h -= (double)(emit_120_h * scroll_grain) / 120.0;

                if (emit_120_v || emit_120_h)
                    evdev_emit_scroll_120(u, emit_120_v, emit_120_h);
            }
        }

        /* ── Route through scroll engine state machine ── */
        int ret = scroll_engine_process(&dev->engine, &buf[i], &out_ev);

        if (ret == 1) {
            /* Forward event unchanged */
            evdev_forward(u, &buf[i]);
        } else if (ret == -1) {
            /* Synthesize middle-click for paste */
            if (cfg_verbose)
                fprintf(stderr, "[autoscroll] → paste passthrough\n");
            evdev_synthesize_click(u);
        }
        /* ret == 0: consumed, do nothing */
    }

    return 0;
}

/* ── Daemonize ────────────────────────────────────────────────────── */
static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) exit(0);  /* parent exits */

    /* Child: new session */
    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }

    /* Second fork to fully detach */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) exit(0);

    /* Close std fds */
    close(0); close(1); close(2);
    open("/dev/null", O_RDONLY);  /* stdin */
    open("/dev/null", O_WRONLY);  /* stdout */
    open("/dev/null", O_WRONLY);  /* stderr */
}

/* ── Main ──────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    /* Parse config file + CLI args */
    config_set_defaults();
    config_parse_file(CONFIG_PATH);
    if (config_parse_args(argc, argv) != 0) {
        config_print_usage(argv[0]);
        return 1;
    }

    /* Just list devices? */
    if (cfg_list_devices)
        return config_list_devices();

    /* Set defaults for optional string fields */
    if (!cfg_match_name)
        cfg_match_name = strdup(DEFAULT_MATCH_NAME);

    /* Create uinput device (must succeed before we grab anything) */
    if (evdev_create_uinput(&g_uinput) < 0)
        return 1;

    /* ── Pinned device path? ───────────────────────────────────── */
    if (cfg_device_path) {
        /* Single device mode: open it directly */
        g_devices = calloc(1, sizeof(struct Device));
        if (!g_devices) return 1;
        g_devices->fd = -1;
        strncpy(g_devices->path, cfg_device_path, sizeof(g_devices->path) - 1);

        int fd = open(cfg_device_path, O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "[autoscroll] Cannot open %s: %s\n",
                    cfg_device_path, strerror(errno));
            cleanup_and_exit(1);
        }
        char name[128] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
            snprintf(name, sizeof(name), "pinned-device");
        strncpy(g_devices->name, name, sizeof(g_devices->name) - 1);
        g_devices->fd = fd;

        scroll_engine_init(&g_devices->engine,
                           (enum scroll_mode)cfg_mode,
                           cfg_deadzone, cfg_gain, cfg_exponent,
                           cfg_max_speed, cfg_position_factor,
                           cfg_tick_ms, cfg_horizontal,
                           cfg_invert_v, cfg_invert_h,
                           cfg_verbose);
        if (cfg_verbose)
            fprintf(stderr, "[autoscroll] Pinned: %s  (%s)\n",
                    g_devices->path, g_devices->name);
    } else {
        /* Discover devices by name regex */
        g_devices = evdev_discover(cfg_match_name);
        if (!g_devices) {
            fprintf(stderr, "[autoscroll] No matching devices found. "
                            "Use -L to list available devices.\n");
            fprintf(stderr, "[autoscroll] Will retry every 10s...\n");

            /* Wait-loop: retry discovery every 10s */
            while (g_running) {
                sleep(10);
                g_devices = evdev_discover(cfg_match_name);
                if (g_devices) break;
            }
        }
    }

    if (!g_devices || !g_running) {
        evdev_destroy_uinput(&g_uinput);
        return g_running ? 1 : 0;
    }

    /* Grab all devices */
    struct Device *dev;
    int any_grabbed = 0;
    for (dev = g_devices; dev; dev = dev->next) {
        if (evdev_grab(dev) == 0) any_grabbed = 1;
    }
    if (!any_grabbed) {
        fprintf(stderr, "[autoscroll] Could not grab any device\n");
        cleanup_and_exit(1);
    }

    /* Daemonize unless -f */
    if (!cfg_foreground) {
        daemonize();
        /* Re-open stderr for logging */
        /* NOTE: in daemon mode, verbose goes nowhere — use journalctl */
    }

    /* Signal handling via signalfd */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0) {
        perror("signalfd");
        cleanup_and_exit(1);
    }

    /* ── Event loop ───────────────────────────────────────────── */
    int ndev = 0;
    for (dev = g_devices; dev; dev = dev->next) ndev++;

    struct pollfd *pfds = calloc((size_t)ndev + 1, sizeof(struct pollfd));
    if (!pfds) { cleanup_and_exit(1); }

    int idx = 0;
    pfds[idx].fd = sfd;
    pfds[idx].events = POLLIN;
    idx++;

    struct Device **dev_index = calloc((size_t)ndev, sizeof(struct Device *));
    if (!dev_index) { free(pfds); cleanup_and_exit(1); }

    int di = 0;
    for (dev = g_devices; dev; dev = dev->next) {
        pfds[idx].fd = dev->fd;
        pfds[idx].events = POLLIN;
        dev_index[di++] = dev;
        idx++;
    }

    if (cfg_verbose)
        fprintf(stderr, "[autoscroll] Running (%d devices)\n", ndev);

    while (g_running) {
        /* ── Tick rate-mode engines ────────────────────────────── */
        double now = now_seconds();
        for (dev = g_devices; dev; dev = dev->next) {
            if (dev->engine.state == ST_SCROLLING && dev->engine.moved) {
                if (dev->engine.mode == MODE_RATE) {
                    double elapsed = now - dev->engine.last_tick_time;
                    double tick_iv = (double)dev->engine.tick_ms / 1000.0;
                    if (elapsed >= tick_iv) {
                        struct input_event tick_ev[16];
                        int n = scroll_engine_tick(&dev->engine, tick_ev, 16, now);
                        for (int i = 0; i < n; i++) {
                            int ret = evdev_forward(&g_uinput, &tick_ev[i]);
                            if (cfg_verbose && tick_ev[i].type == EV_REL) {
                                const char *cname = "?";
                                if (tick_ev[i].code == REL_WHEEL_HI_RES) cname = "WHEEL_HI_RES";
                                else if (tick_ev[i].code == REL_WHEEL) cname = "WHEEL";
                                else if (tick_ev[i].code == REL_HWHEEL_HI_RES) cname = "HWHEEL_HI_RES";
                                else if (tick_ev[i].code == REL_HWHEEL) cname = "HWHEEL";
                                fprintf(stderr, "[autoscroll] uinput write %s=%d (%s)\n",
                                        cname, tick_ev[i].value, ret == 0 ? "ok" : "FAIL");
                            }
                        }
                    }
                }
            }
        }

        /* ── Poll for events (minimal timeout so ticks stay smooth) ── */
        int poll_timeout = 5; /* 5ms — tight enough for 200Hz tick */
        int ret = poll(pfds, (nfds_t)(ndev + 1), poll_timeout);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Check signal */
        if (pfds[0].revents & POLLIN) {
            struct signalfd_siginfo si;
            if (read(sfd, &si, sizeof(si)) > 0) {
                if (cfg_verbose)
                    fprintf(stderr, "[autoscroll] Signal %d, exiting\n", si.ssi_signo);
                break;
            }
        }

        /* Check devices */
        for (int i = 1; i < idx; i++) {
            if (pfds[i].revents & POLLIN) {
                if (process_device(dev_index[i - 1], &g_uinput) < 0) {
                    /* Device removed — trigger service restart */
                    fprintf(stderr, "[autoscroll] Device lost, exiting\n");
                    g_running = 0;
                    break;
                }
            }
            if (pfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                fprintf(stderr, "[autoscroll] Device error on %s\n",
                        dev_index[i - 1]->path);
                g_running = 0;
                break;
            }
        }
    }

    free(pfds);
    free(dev_index);
    cleanup_and_exit(0);
    return 0;
}
