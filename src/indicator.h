#ifndef INDICATOR_H
#define INDICATOR_H

/* ── State broadcast via UNIX socket ───────────────────────────── */
/* The daemon opens a UNIX socket at /tmp/autoscroll.sock
 * and broadcasts one line per state change:
 *   "dx dy active\n"
 * where dx,dy = signed px from anchor (0 when idle), active=0|1.
 */

int  indicator_init(void);
void indicator_broadcast(int dx, int dy, int active);
void indicator_shutdown(void);

#endif
