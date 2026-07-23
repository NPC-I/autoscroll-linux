# autoscroll-linux

**System-wide Windows-style middle-click autoscroll for Linux.**  
Single binary, zero runtime dependencies, single evdev+uinput backend proven to
work on **every Linux desktop**: X11, GNOME Wayland, KDE Wayland, wlroots
(cosmic-comp, Sway, Hyprland), Gamescope, Weston.

Press and hold the middle mouse button → move the pointer → content scrolls
proportional to distance from anchor. Quick press+release passes through as a
normal middle-click (paste preserved).

## Quick Start

```bash
# Install the system service
sudo cmake --install build
sudo cp service/autoscroll.service /etc/systemd/system/
sudo cp udev/99-autoscroll.rules /etc/udev/rules.d/
sudo systemctl daemon-reload
sudo systemctl enable --now autoscroll
```

Or run directly (must be root for `/dev/uinput`):

```bash
sudo build/src/autoscroll -f -v
```

That's it. Hold middle-click anywhere and move the mouse to scroll.

## Usage

```
autoscroll [OPTIONS]

Scroll behavior:
  -m, --mode=rate|position     Scroll feel              [rate]
  -d, --deadzone=PX            Deadzone before scroll   [18]
  -g, --gain=FLOAT             Rate-mode speed factor   [0.200]
  -e, --exponent=FLOAT         Rate-mode curve          [1.30]
      --max-speed=FLOAT        Notches/sec cap          [40.0]
  -p, --position-factor=FLOAT  Position sensitivity     [0.06]
  -t, --tick=MS                Rate-mode tick           [15]
  -H, --no-horizontal          Disable horizontal scrolling
      --invert-v               Flip vertical scroll
      --invert-h               Flip horizontal scroll

Device selection:
  -L, --list-devices           List input devices
  -D, --device=PATH            Pin specific /dev/input/eventN
      --match-name=REGEX       Device name filter regex    [(none)]

Runtime:
  -f, --foreground             Don't daemonize
  -v, --verbose                Log state transitions
  -V, --version
  -h, --help
```

### Interactive Autoscroll Behavior

1. **Press and hold** middle button → anchor is set at cursor position
2. **Move pointer** while holding → scroll speed proportional to distance from anchor
   - **Rate mode** (default): scroll continues at computed speed even if you stop moving
   - **Position mode**: scrolls only while actively moving, roughly 1:1
3. **Release** middle button → scrolling stops
4. **Quick press+release** (within deadzone) → passes through as normal middle-click → paste works

## Configuration

Edit `/etc/autoscroll.conf`:

```ini
mode = rate
deadzone = 18
gain = 0.020
exponent = 1.30
max_speed = 40.0
tick_ms = 15
position_factor = 0.06
horizontal = true
invert_v = false
invert_h = false
match_name =
```

CLI flags **override** config file values.

## Build from Source

```bash
# Dependencies: cmake, gcc, make, glibc
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build          # run unit tests
sudo cmake --install build       # install to /usr/local
```

### Distro Packages

**Debian/Ubuntu:**
```bash
# From source tree:
dpkg-buildpackage -us -uc
sudo dpkg -i ../autoscroll_1.0.0-1_*.deb
```

**Fedora/RHEL:**
```bash
rpmbuild -ba packaging/autoscroll.spec
sudo dnf install ~/rpmbuild/RPMS/x86_64/autoscroll-*.rpm
```

**Arch Linux:**
```bash
makepkg -si
```

## How It Works

```
Physical mouse
    │ evdev events
    ▼
autoscroll daemon (EVIOCGRAB — exclusive access)
    │
    ├── IDLE ──[BTN_MIDDLE down]──→ HELD (set anchor)
    │   ├── [move inside deadzone] → stay HELD
    │   ├── [move outside deadzone] → SCROLLING
    │   │   └── emit REL_WHEEL_HI_RES + REL_WHEEL
    │   └── [BTN_MIDDLE up]
    │       ├── inside deadzone → synthesize click → uinput (paste passthrough)
    │       └── outside deadzone → consumed, return to IDLE
    │
    ▼
uinput virtual device
    │
    ▼
Compositor (X11/Wayland) → delivers scroll events to focused window
```

Key design decisions:

- **evdev+uinput** (not libei/portal): zero dependencies, works on all Linux
  desktops including X11, no portal dialogs, no D-Bus dependency
- **System service** (not user): runs as root with `DevicePolicy=closed`
  sandboxing for /dev/uinput access
- **Hold-to-scroll** (not click-to-toggle): matches modern UX, proven by
  mmb-autoscroll; quick click within deadzone passes through as paste
- **Hi-res wheel** (`REL_WHEEL_HI_RES`, 120 units/notch): smooth scrolling
  with backward-compatible `REL_WHEEL` fallback

## Coverage

| Desktop | Status | Notes |
|---------|--------|-------|
| X11 (any) | ✅ Works | evdev+uinput, silent, zero setup |
| GNOME Wayland | ✅ Works | Proven by mmb-autoscroll |
| KDE Wayland | ✅ Works | evdev+uinput |
| cosmic-comp (wlroots) | ✅ Works | evdev+uinput |
| Sway / Hyprland | ✅ Works | evdev+uinput |
| Gamescope | ✅ Works | evdev+uinput |
| Weston | ✅ Works | evdev+uinput |

## Known Caveats

| # | Caveat | Impact |
|---|--------|--------|
| C1 | **Cursor freezes during autoscroll** | Pointer REL events are suppressed while autoscrolling — cursor won't move. Intentional, matches Windows behavior. |
| C2 | **Cursor jumps on exit** | When autoscroll ends, the compositor catches up with the accumulated relative motion. The cursor may jump to where it would have been. Inherent to uinput-based approach (same as Windows). |
| C3 | **No visual feedback** | Wayland has no cursor icon API from userspace. Check status via `journalctl -u autoscroll`. |
| C4 | **Game grab conflicts** | If a game has already grabbed the mouse (EVIOCGRAB EBUSY), the device is skipped. Run `autoscroll -L` to see which devices are available. |
| C5 | **X11 acceleration loss** | The uinput virtual device has default pointer acceleration. Use `xinput --set-prop` to tune. |
| C6 | **SELinux/AppArmor** | Enforcing policies may block uinput on Fedora. Use `audit2allow` to generate the necessary policy. |
| C7 | **Minimum kernel: 5.0** | `REL_WHEEL_HI_RES` requires Linux 5.0+. Older kernels get only `REL_WHEEL` (discrete notches). |
| C8 | **VT switch** | EVIOCGRAB persists across virtual terminal switches. The TTY won't see the mouse events (uinput forwards them). Minor issue. |
| C9 | **Screen lock** | If middle-click is held when the screen locks, autoscroll stays active (cursor frozen). Unlikely but real. Unlock and click to release. |
| C10 | **WSL unsupported** | Windows Subsystem for Linux has no `/dev/input/`. Graceful error. |
| C11 | **Flatpak/Snap** | Host-level install required. The daemon runs outside the sandbox. |

## Architecture

```
autoscroll-linux/
├── CMakeLists.txt              # cmake ≥3.16, C99
├── README.md
├── LICENSE                     # MIT
├── VERSION                     # 1.0.0
├── PLAN.md                     # Full implementation plan
├── src/
│   ├── main.c                  # Entry, event loop, daemonize, signalfd
│   ├── config.c/h              # INI parser + CLI arg parser
│   ├── scroll_engine.c/h       # Pure state machine (testable without evdev)
│   └── evdev_backend.c/h       # Device discovery, EVIOCGRAB, uinput
├── tests/
│   ├── CMakeLists.txt
│   └── test_scroll_engine.c    # 18 unit tests (headless)
├── service/
│   └── autoscroll.service      # System service (DevicePolicy=closed)
├── udev/
│   └── 99-autoscroll.rules     # /dev/uinput permissions
├── man/
│   └── autoscroll.1            # Man page
├── debian/                     # Debian packaging
├── packaging/
│   ├── autoscroll.spec         # Fedora/RHEL RPM
│   └── PKGBUILD                # Arch Linux
└── .github/workflows/
    └── build.yml               # CI: Ubuntu, Fedora, Arch
```

## Why evdev+uinput (not libei)?

The plan explicitly chooses evdev+uinput for V1. libei (v1.5/1.6) is an
optional Wayland-native alternative through xdg-desktop-portal RemoteDesktop.

| Factor | evdev+uinput | libei+portal |
|--------|-------------|--------------|
| Dependencies | None (just libc) | libei, liboeffis, D-Bus, portal |
| Works on X11 | ✅ Yes | ❌ No (Wayland only) |
| Works on Wayland | ✅ Yes (proven) | ✅ Yes |
| Root required? | ✅ System service | ❌ No (portal auth) |
| User setup | ❌ None (install + enable) | ✅ Portal dialog (first session) |
| Compositor-aware | ❌ No (below compositor) | ✅ Yes |
| Code complexity | ~700 lines | Higher (D-Bus + libei IPC) |
| Proven in production | ✅ mmb-autoscroll | ⚠️ InputLeap (experimental) |

A future V2 may add an optional libei backend for non-root operation.

## License

MIT
