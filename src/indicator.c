#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "indicator.h"

#define SOCK_PATH_ENV "XDG_RUNTIME_DIR"
#define SOCK_NAME     "/autoscroll.sock"

static int g_sock_fd = -1;
static int g_client_fd = -1;  /* single connected client */
static char g_sock_path[] = "/run/autoscroll.sock";

int indicator_init(void)
{
    /* Remove stale socket */
    unlink(g_sock_path);

    g_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_sock_fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);

    if (bind(g_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[autoscroll] indicator bind failed: %s\n", strerror(errno));
        close(g_sock_fd);
        g_sock_fd = -1;
        return -1;
    }

    /* Allow anyone to connect */
    chmod(g_sock_path, 0666);

    listen(g_sock_fd, 1);
    fcntl(g_sock_fd, F_SETFL, O_NONBLOCK);
    return 0;
}

void indicator_broadcast(int dx, int dy, int active)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%d %d %d\n", dx, dy, active);

    /* Try to accept a new client */
    if (g_client_fd < 0) {
        struct sockaddr_un dummy;
        socklen_t len = sizeof(dummy);
        g_client_fd = accept(g_sock_fd, (struct sockaddr *)&dummy, &len);
        if (g_client_fd >= 0)
            fcntl(g_client_fd, F_SETFL, O_NONBLOCK);
    }

    if (g_client_fd >= 0) {
        if (write(g_client_fd, buf, (size_t)n) < 0) {
            /* Client disconnected */
            close(g_client_fd);
            g_client_fd = -1;
        }
    }
}

void indicator_shutdown(void)
{
    if (g_client_fd >= 0) close(g_client_fd);
    if (g_sock_fd >= 0) close(g_sock_fd);
    unlink(g_sock_path);
}
