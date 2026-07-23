/**
 * autoscroll-indicator.c — Wayland overlay showing autoscroll pull.
 * Uses GTK3 + gtk-layer-shell for the transparent overlay.
 *
 * Build: gcc -std=c11 $(pkg-config --cflags gtk+-3.0 gtk-layer-shell-0) \
 *            -o autoscroll-indicator autoscroll-indicator.c \
 *            $(pkg-config --libs gtk+-3.0 gtk-layer-shell-0) -lm
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

static GtkWidget *g_window = NULL;
static int        g_sock_fd = -1;
static int        g_dx = 0, g_dy = 0, g_active = 0;
static int        g_ptr_x = 0, g_ptr_y = 0;  /* cursor pos on overlay */

static int connect_to_daemon(void)
{
    const char *path = "/run/autoscroll.sock";
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        { close(fd); return -1; }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

static void read_socket(void)
{
    if (g_sock_fd < 0) { g_sock_fd = connect_to_daemon(); return; }
    char buf[256];
    int n = (int)read(g_sock_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(g_sock_fd); g_sock_fd = -1; return; }
    buf[n] = '\0';
    char *line = buf;
    while (line) {
        char *nl = strchr(line, '\n');
        if (!nl) break;
        *nl = '\0';
        sscanf(line, "%d %d %d", &g_dx, &g_dy, &g_active);
        line = nl + 1;
    }
}

/* ── GTK3 draw callback ──────────────────────────────────────────── */
static gboolean draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    (void)data;
    int width  = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Clear transparent */
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);

    if (!g_active) return FALSE;

    double cx = (double)g_ptr_x;
    double cy = (double)g_ptr_y;
    double ox = (double)g_dx;
    double oy = (double)g_dy;
    double dist = hypot(ox, oy);
    if (dist < 1.0) return FALSE;

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Pull line from anchor to cursor */
    cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.7);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, cx - ox, cy - oy);
    cairo_line_to(cr, cx, cy);
    cairo_stroke(cr);

    /* Arrowhead at cursor */
    double angle = atan2(oy, ox);
    double al = 12.0;
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx - al * cos(angle - 0.4), cy - al * sin(angle - 0.4));
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx - al * cos(angle + 0.4), cy - al * sin(angle + 0.4));
    cairo_stroke(cr);

    /* Anchor dot (red circle) */
    cairo_arc(cr, cx - ox, cy - oy, 4.0, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 1.0, 0.3, 0.3, 0.7);
    cairo_fill(cr);

    /* Distance ring around cursor (blue circle) */
    double rr = 8.0 + fmin(dist * 0.05, 40.0);
    cairo_arc(cr, cx, cy, rr, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.3);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    /* Speed bar above cursor */
    double speed = fmin(dist * 0.06, 160.0) / 160.0;
    double bw = 80.0, bh = 6.0;
    cairo_rectangle(cr, cx - bw / 2, cy - 30, bw * speed, bh);
    cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.6);
    cairo_fill(cr);
    cairo_rectangle(cr, cx - bw / 2, cy - 30, bw, bh);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean timer_cb(gpointer data)
{
    (void)data;
    read_socket();

    /* Track cursor position */
    if (g_window) {
        GdkDisplay *display = gtk_widget_get_display(g_window);
        GdkWindow *win = gtk_widget_get_window(g_window);
        if (win && display) {
            GdkSeat *seat = gdk_display_get_default_seat(display);
            if (seat) {
                GdkDevice *pointer = gdk_seat_get_pointer(seat);
                if (pointer) {
                    int wx, wy;
                    gdk_window_get_device_position(win, pointer, &wx, &wy, NULL);
                    if (wx >= 0 && wy >= 0) { g_ptr_x = wx; g_ptr_y = wy; }
                }
            }
        }
    }

    if (g_window) gtk_widget_queue_draw(g_window);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_window), "autoscroll-indicator");
    gtk_window_set_decorated(GTK_WINDOW(g_window), FALSE);
    gtk_widget_set_app_paintable(g_window, TRUE);

    /* RGBA visual for transparency */
    GdkScreen *screen = gtk_widget_get_screen(g_window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(g_window, visual);

    /* Layer shell: overlay layer, no margins, anchor all edges */
    gtk_layer_init_for_window(GTK_WINDOW(g_window));
    gtk_layer_set_layer(GTK_WINDOW(g_window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(GTK_WINDOW(g_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(g_window), -1);
    gtk_layer_set_keyboard_interactivity(GTK_WINDOW(g_window), FALSE);

    /* Drawing area */
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 1, 1);
    g_signal_connect(G_OBJECT(da), "draw", G_CALLBACK(draw_cb), NULL);
    gtk_container_add(GTK_CONTAINER(g_window), da);

    gtk_widget_set_visible(g_window, TRUE);
    g_timeout_add(16, timer_cb, NULL);
    gtk_main();
    return 0;
}
