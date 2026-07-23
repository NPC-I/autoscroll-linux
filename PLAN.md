# Autoscroll-Linux — Complete Implementation Plan

## Overview

System-wide Windows-style middle-click autoscroll for Linux. Written in C (C99).
Single binary, zero runtime dependencies, single evdev+uinput backend proven to
work on **every Linux desktop**: X11, GNOME Wayland, KDE Wayland, wlroots
(cosmic-comp, Sway, Hyprland), Gamescope, Weston.

Researched existing implementations (notably `CultureCrypto/mmb-autoscroll`,
created June 2026) to validate the approach and avoid known pitfalls.

## Key Research Findings

| Finding | Source | Impact on Plan |
|---------|--------|----------------|
| evdev+uinput works on GNOME/Wayland | mmb-autoscroll (Python, June 2026) | **No libei backend needed** — single evdev backend covers everything |
| Hold-to-scroll (not click-to-toggle) matches Windows | mmb-autoscroll UX | Adopt hold-to-scroll pattern |
| Hi-res wheel output (REL_WHEEL_HI_RES=120 per notch) | mmb-autoscroll code | Must emit hi-res wheel for smooth scrolling |
| Multi-device support (grab ALL matching mice) | mmb-autoscroll code | Grab all devices matching name regex, not just one |
| Device-hotplug waiting (10s retry) | mmb-autoscroll code | Don't exit-loop on missing mouse; wait and retry |
| System service with DevicePolicy=closed hardening | mmb-autoscroll.service | Ship as system service, not user service |
| libinput on-button scrolling exists but GNOME doesn't expose it | libinput docs | The problem is real — compositors don't expose the API |
| wlr_virtual_pointer_v1 protocol exists | wlr-protocols | Alternative injection path for future (not needed now) |
| libei packaged in Debian/Arch | Package repos | Available as optional enhancement for V2 |

## Coverage Matrix

| Desktop | Status | Notes |
|---------|--------|-------|
| X11 (any) | ✅ Works | evdev+uinput, silent, zero setup |
| GNOME Wayland | ✅ Works | evdev+uinput (proven by mmb-autoscroll) |
| KDE Wayland | ✅ Works | evdev+uinput |
| cosmic-comp (wlroots) | ✅ Works | evdev+uinput |
| Sway / Hyprland | ✅ Works | evdev+uinput |
| Gamescope | ✅ Works | evdev+uinput |
| Weston | ✅ Works | evdev+uinput |

## UX Design: Hold-to-Scroll (not Click-to-Toggle)

Research from mmb-autoscroll shows the correct UX:

- **Press and hold** middle button → anchor is set at current cursor position
- **Move pointer** while holding → scroll speed proportional to distance from anchor
- **Release** middle button → scrolling stops, normal mode resumes
- **Quick press+release** within deadzone → passes through as normal middle-click (paste preserved)

This avoids the click-to-toggle issue (where you need a second click to stop) and
is closer to actual Windows autoscroll behavior.

**Rate mode** (default, Windows-feel): scroll speed follows
`notches/sec = gain × distance^exponent`, capped at max_speed. Continues scrolling
while held even if the pointer stops moving.

**Position mode** (libinput-feel): scrolls only while actively moving, roughly 1:1.

## Architecture

```
autoscroll
  │
  ├─ parse_config()          # /etc/autoscroll.conf (INI-style)
  ├─ parse_args()            # CLI overrides config
  ├─ daemonize()             # Fork to background (unless -f)
  │
  ├─ signalfd(SIGINT, SIGTERM, SIGHUP)
  ├─ discover_devices()      # Find ALL matching mice (regex name match)
  │   └─ If none found → wait 10s, retry (loop until one appears)
  ├─ for each device:
  │   ├─ open() / EVIOCGRAB
  │   └─ create ScrollEngine (state machine per device)
  ├─ create_uinput()         # Single virtual output device for ALL sources
  │
  ├─ event_loop()
  │   ├─ poll(device_fds[] + signalfd, timeout)
  │   ├─ read events → dispatch to ScrollEngine.process()
  │   │   ├─ IDLE: forward all events to uinput
  │   │   ├─ HELD (middle down): accumulate motion, suppress REL
  │   │   └─ RELEASED: optionally synthesize middle-click if within deadzone
  │   ├─ tick() → if any engine in rate mode + HELD + MOVED:
  │   │     inject_scroll(REL_WHEEL_HI_RES)
  │   └─ device removed → drop device, exit if none left
  │
  └─ cleanup()
      ├─ ungrab all devices
      └─ destroy uinput
```

## State Machine (per device)

```
IDLE ──[BTN_MIDDLE down]──→ HELD
  │                           │
  │                    ┌──────┴──────┐
  │                    │             │
  │               [within         [outside
  │               deadzone]      deadzone]
  │                    │             │
  │                    │        ┌────┴────┐
  │                    │        │         │
  │                    │    SCROLLING  (rate)
  │                    │         │    or
  │                    │    (position)  │
  │                    │         │      │
  │              [BTN_MIDDLE up] │      │
  │                    │         │      │
  │         synthesize click     │      │
  │         → forward to uinput  │      │
  │                    │         │      │
  └────────────────────┴─────────┴──────┘
                        │
                      IDLE
```

Quick press+release inside deadzone: BTN_MIDDLE down+up synthesized through
uinput → application sees normal middle-click → paste works.

## Project Structure

```
autoscroll-linux/
├── CMakeLists.txt
├── README.md
├── LICENSE                     # MIT
├── .gitignore
├── VERSION                     # "1.0.0"
├── src/
│   ├── CMakeLists.txt
│   ├── main.c                  # Entry, event loop, daemonize, signal handling
│   ├── config.c                # Config file + CLI arg parsing
│   ├── config.h
│   ├── scroll_engine.c         # Pure state machine (testable without evdev)
│   ├── scroll_engine.h
│   ├── evdev_backend.c         # Device discovery, EVIOCGRAB, uinput, forwarding
│   └── evdev_backend.h
├── tests/
│   ├── CMakeLists.txt
│   └── test_scroll_engine.c    # Unit tests for ScrollEngine (headless)
├── .github/
│   └── workflows/
│       └── build.yml           # CI: build on Ubuntu, Fedora, Arch containers
├── debian/
│   ├── control
│   ├── copyright
│   ├── changelog
│   ├── rules
│   ├── postinst
│   ├── prerm
│   └── source/
│       └── format
├── packaging/
│   ├── autoscroll.spec         # Fedora/RHEL RPM
│   └── PKGBUILD                # Arch Linux AUR
├── service/
│   └── autoscroll.service      # System service (system service, not user)
├── udev/
│   └── 99-autoscroll.rules
└── man/
    └── autoscroll.1
```

## CLI

```
autoscroll [OPTIONS]

Scroll behavior:
  -m, --mode=rate|position   Scroll feel                      [rate]
  -d, --deadzone=PX          Deadzone before scroll starts    [18]
  -g, --gain=FLOAT           Rate-mode speed factor           [0.020]
  -e, --exponent=FLOAT       Rate-mode curve exponent         [1.30]
      --max-speed=FLOAT      Rate-mode notches/sec cap        [40.0]
  -p, --position-factor=FLOAT Position-mode sensitivity      [0.06]
  -t, --tick=MS              Rate-mode tick interval          [15]
  -H, --no-horizontal        Disable horizontal scrolling
      --invert-v             Flip vertical scroll direction
      --invert-h             Flip horizontal scroll direction

Device selection:
  -L, --list-devices         List input devices and exit
  -D, --device=PATH          Pin explicit /dev/input/eventN
      --match-name=REGEX     Device name match pattern        [(?i)mouse]

Runtime:
  -f, --foreground           Don't daemonize
  -v, --verbose              Log state transitions
  -V, --version
  -h, --help
```

## Config File (`/etc/autoscroll.conf`)

INI-style key=value:
```ini
mode = rate
deadzone = 18
gain = 0.200
exponent = 1.30
max_speed = 40.0
tick_ms = 15
position_factor = 0.06
horizontal = true
invert_v = false
invert_h = false
match_name = (?i)mouse
```

CLI flags override config file values.

## CMake

```cmake
cmake_minimum_required(VERSION 3.16)
project(autoscroll VERSION 1.0.0 LANGUAGES C)
set(CMAKE_C_STANDARD 99)

# Single backend, always compiled (zero deps beyond libc)
add_executable(autoscroll
    src/main.c
    src/config.c
    src/scroll_engine.c
    src/evdev_backend.c
)

target_link_libraries(autoscroll PRIVATE rt)  # clock_gettime on older glibc

install(TARGETS autoscroll RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES service/autoscroll.service DESTINATION lib/systemd/system)
install(FILES udev/99-autoscroll.rules DESTINATION lib/udev/rules.d)
install(FILES man/autoscroll.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
```

## Service File

```ini
[Unit]
Description=autoscroll-linux — system-wide middle-button autoscroll
Documentation=man:autoscroll(1)
StartLimitIntervalSec=0
After=systemd-udevd.service
Wants=systemd-udevd.service

[Service]
Type=simple
EnvironmentFile=-/etc/autoscroll.conf
ExecStart=/usr/bin/autoscroll
Restart=always
RestartSec=2
DevicePolicy=closed
DeviceAllow=/dev/uinput rw
DeviceAllow=char-input rw
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

## Hi-Res Wheel Output

Emit REL_WHEEL_HI_RES for smoothed scrolling (120 units = 1 notch).
Also emit REL_WHEEL for backwards compatibility with apps that don't
recognize hi-res events:

```c
void emit_scroll(int v_notches, int h_notches) {
    // Hi-res (120 units per notch) — smooth scrolling
    if (v_notches)
        write_event(uinput_fd, EV_REL, REL_WHEEL_HI_RES, v_notches * 120);
    if (h_notches)
        write_event(uinput_fd, EV_REL, REL_HWHEEL_HI_RES, h_notches * 120);
    write_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
    
    // Low-res (backwards compatibility)
    if (v_notches)
        write_event(uinput_fd, EV_REL, REL_WHEEL, v_notches);
    if (h_notches)
        write_event(uinput_fd, EV_REL, REL_HWHEEL, h_notches);
    write_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
}
```

## Implementation Notes

### Fractional Scroll Accumulator (Critical — C19)

Without fractional accumulators, slow scrolling stutters: 0, 1, 0, 1 notches
per tick instead of smooth 0.5 per tick. The ScrollEngine MUST maintain
per-axis float accumulators:

```c
struct ScrollEngine {
    // ... state fields ...
    double acc_v;  // accumulated fractional vertical notches
    double acc_h;  // accumulated fractional horizontal notches
};

// Each tick: accumulate fractional scroll, emit only integer part
double velocity = gain * pow(distance - deadzone, exponent);
acc_v += velocity * dt;  // accumulate fractional notches
int emit_v = (int)acc_v; // only integer part emitted
acc_v -= emit_v;         // keep the remainder

double acc_h += /* horizontal velocity */ * dt;
int emit_h = (int)acc_h;
acc_h -= emit_h;

emit_scroll(emit_v, emit_h);
```

Accumulator initialized to 0.0 on middle-down. Drained to 0.0 on release.

### Position Mode Motion Accumulator

In position mode, add_motion() immediately converts dx/dy to notches via
`position_factor`. Accumulate fractional remainders, emit integer part:

```c
void add_motion(int dx, int dy) {
    if (!held || !moved) return;
    acc_v += -dy * position_factor;  // move down → scroll negative
    acc_h += dx * position_factor;
    // Drain immediately for position mode responsiveness
    int ev = (int)acc_v; acc_v -= ev;
    int eh = (int)acc_h; acc_h -= eh;
    emit_scroll(ev, eh);
}
```

### Event Batching (C26)

After poll() returns, read() from a grabbed evdev device may return
multiple struct input_event's in a single read. Use read() in a loop
until EAGAIN:

```c
struct input_event buf[64];
int n = read(fd, buf, sizeof(buf));
if (n < 0) {
    if (errno == EAGAIN) break;   // no more events
    /* handle ENODEV / ENXIO */;
}
int count = n / sizeof(struct input_event);
for (int i = 0; i < count; i++) {
    process_event(&buf[i], engine);
}
```

### EVIOCGRAB Failure Handling (C27)

| Error | Cause | Action |
|-------|-------|--------|
| EBUSY | Device already grabbed (game, another instance) | Skip device, try next, log warning |
| EACCES | Permission denied (not root, not in input group) | Print error message, try next |
| ENODEV/ENXIO | Device disconnected between discovery and grab | Skip device |

If ALL devices fail EVIOCGRAB, exit with code and message.

### Minimum Kernel Version

Linux 5.0+ required for REL_WHEEL_HI_RES. On older kernels, the uinput
creation should omit hi-res axes and fall back to REL_WHEEL only. Detect
at runtime via EVIOCGBIT on the uinput fd (check if kernel accepts the
bit).

### Device Exclusion Checks (C24, C25)

Touchpad exclusion (comprehensive):
- Capability: BTN_TOOL_FINGER present → touchpad
- Capability: ABS_MT_POSITION_X present → multi-touch
- Name contains (case-insensitive): touchpad, synaptics, alps, focaltech,
  cypress, elan, etps/2, trackpad

Tablet exclusion:
- Capability: BTN_STYLUS or BTN_TOOL_PEN present → tablet
- Name contains (case-insensitive): wacom, pentablet, penpad, pen, digitizer

Virtual device exclusion:
- Name contains (case-insensitive): virtual, uinput, autoscroll (our own device)

## Caveat Register (Updated from Research)

| # | Caveat | Resolution | Corrected? |
|---|--------|------------|------------|
| C1 | ~~GNOME blocks uinput~~ | **WRONG** — evdev+uinput works on GNOME (proven by mmb-autoscroll) | ✅ Corrected |
| C2 | Wayland cursor lock | Suppress REL events — cursor freezes (same effect) | |
| C3 | Middle-click paste | Hold-based activation; quick release within deadzone → synthetic click → paste works | |
| C4 | Cursor jump on exit | Inherent (same as Windows). Documented. | |
| C5 | No visual feedback | stderr + journalctl. Wayland has no cursor icon API from userspace. | |
| C6 | Touchpad false positive | Exclude by BTN_TOOL_FINGER + name match (touchpad/synaptics/ELAN) | |
| C7 | Scroll flooding | Cap at max_speed (40 notches/sec). Not an issue with hi-res wheel. | |
| C8 | Device hot-unplug | Remove device from tracking. Exit if none left. systemd restarts. | |
| C9 | Suspend/resume | systemd restarts service after resume | |
| C10 | Game grab conflict | EVIOCGRAB returns EBUSY → skip device, continue with others | |
| C11 | X11 acceleration loss | Documented; xinput --set-prop workaround on virtual device | |
| C12 | /dev/uinput perms | udev rule in package. System service runs as root (with sandboxing). | |
| C13 | WSL unsupported | Graceful error: no /dev/input | |
| C14 | Flatpak/Snap confinement | Document: host-level install required | |
| C15 | ~~libei not installed~~ | **REMOVED** — not needed. Single evdev backend works everywhere. | ✅ Removed |
| C16 | No mouse connected at boot | Wait loop: poll 10s, retry. Avoids systemd restart storm. | New |
| C17 | Multiple mice | Grab ALL matching devices; create one ScrollEngine per device | New |
| C18 | Mouse name regex misses device | `--match-name` flag + `--list-devices` for discovery | New |
| C19 | Scroll accumulator precision | Fractional notches must use accumulator (float remainder) or slow scroll stutters | New — critical |
| C20 | SELinux/AppArmor on Fedora | SELinux enforcing may block uinput. Document `audit2allow` workaround. | New |
| C21 | Minimum kernel version | REL_WHEEL_HI_RES needs Linux 5.0+. Document in README. | New |
| C22 | VT switch grab persistence | EVIOCGRAB persists across VTs. TTY won't see mouse (uinput forwarded). Minor. | New |
| C23 | Screen lock autoscroll risk | Middle-click held on lock screen → HELD state → cursor locks. Unlikely but real. | New |
| C24 | Touchpad exclusion incomplete | Expand name list: Alps, FocalTech, Cypress, ETPS/2, ELAN, TouchPad | New — expanded C6 |
| C25 | Tablet false positive | Exclude: Wacom, PenTablet, PenPad. Check BTN_STYLUS/BTN_TOOL_PEN. | New |
| C26 | evdev read batching | read() from grabbed device returns multiple events. Must batch-process per poll(). | New — implementation |
| C27 | EVIOCGRAB failure details | Handle EBUSY → skip, EACCES → error msg, ENODEV → try next device | New — expanded C10 |

## What Changed From Previous Plan

1. **Removed libei backend** — evdev+uinput works on all compositors (GNOME included). Less code, simpler architecture, no optional dependencies.
2. **Removed backend abstraction** — single backend means no vtable, no compositor detection routing.
3. **Changed UX from click-to-toggle to hold-to-scroll** — matches Windows behavior, proven by mmb-autoscroll.
4. **Added hi-res wheel output** — REL_WHEEL_HI_RES for smooth scrolling.
5. **Added multi-device support** — grab all matching mice, not one.
6. **Added device-hotplug waiting** — no restart storms when no mouse connected.
7. **Changed from user service to system service** — with DevicePolicy=closed sandboxing (proven by mmb-autoscroll).
8. **Config at /etc/autoscroll.conf** instead of ~/.config/autoscroll/config.
9. **Simplified CLI** — removed `--backend`, `--force-uinput`, `--long-press` flags.
10. **Added rate mode** with exponent curve (gain × distance^exponent).
11. **Scroll engine is pure C** with no evdev dependency — testable headlessly.
12. **Corrected caveat C1** — GNOME does NOT block uinput; assumption was wrong.
13. **Removed caveat C15** — libei not needed.
14. **Added caveats C16-C18** — new findings from research.

## libei Research — Why Not V1 (and When It Makes Sense)

libei (Emulated Input, v1.5/1.6) is a stable, packaged alternative that provides
Wayland-native input injection through the xdg-desktop-portal RemoteDesktop
interface. It works via: portal D-Bus handshake → `ConnectToEIS()` → libei ↔ libeis
direct connection. libei supports both **sender mode** (emit events) and **receiver
mode** (capture events) — necessary for autoscroll.

**Why we chose evdev+uinput over libei for V1:**

| Factor | evdev+uinput | libei+portal |
|--------|-------------|--------------|
| Dependencies | None (just libc) | libei, liboeffis, D-Bus, portal |
| Works on X11 | ✅ Yes | ❌ No (Wayland only) |
| Works on Wayland (all) | ✅ Yes (proven) | ✅ Yes |
| Root required? | ✅ System service | ❌ No (portal auth) |
| User setup needed | ❌ None (install + enable) | ✅ Portal dialog (1st session) |
| Compositor-aware | ❌ No (below compositor) | ✅ Yes |
| Code complexity | Low (~700 lines) | Higher (D-Bus + libei IPC) |
| Proven in production | ✅ mmb-autoscroll | ⚠️ InputLeap (experimental) |

For a packaged tool: evdev+uinput means install → enable → works. No dialogs,
no D-Bus, no extra deps. libei is the right path for a **future V2** that supports
portal-based auth (no root), but adds minimal value for V1 since uinput works
everywhere.

**When to add libei:**
- If a compositor starts blocking uinput (unlikely, but possible in future Wayland specs)
- If we want a non-root installation path
- If we want to support "smooth" Wayland-native cursor feedback
- As an optional compile-time backend (CMake: `find_package(libei)`)
