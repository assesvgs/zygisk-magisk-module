#include "ipc_client.hpp"
#include "logging.hpp"
#include <common/consts.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

static int g_sock = -1;
static bool g_available = true;

static int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char *)buf + total, len - total);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

bool zygisk_is_available() {
    if (!g_available) LOGW("zygiskd unavailable (circuit breaker active)");
    return g_available;
}

int zygisk_connect() {
    if (!g_available) return -1;
    if (g_sock >= 0) return g_sock;
    LOGD("Connecting to zygiskd...");

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ZYGISK_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    for (int delay = 100; delay <= 3000; delay *= 2) {
        if (!g_available) return -1;
        g_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_sock < 0) return -1;
        if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            LOGD("Connected to zygiskd");
            return g_sock;
        }
        close(g_sock);
        g_sock = -1;
        usleep(delay * 1000);
        if (delay < 1600) continue;
        delay = 3000;
    }
    LOGE("Cannot connect to zygiskd (socket=%s)", ZYGISK_SOCKET_PATH);
    g_available = false;
    return -1;
}

void zygisk_disconnect() {
    if (g_sock >= 0) { close(g_sock); g_sock = -1; }
}

int zygisk_request(int opcode, int pid, int32_t *out_uid, uint32_t *out_flags, int *out_fd) {
    if (zygisk_connect() < 0) return -1;

    zygisk_request_header_t req{};
    req.opcode = opcode;
    req.pid = pid;

    if (write(g_sock, &req, sizeof(req)) < 0) { zygisk_disconnect(); return -1; }

    zygisk_response_header_t resp{};
    if (read_all(g_sock, &resp, sizeof(resp)) < 0) { zygisk_disconnect(); return -1; }

    // memcpy 从 packed struct 到对齐变量，避免 ARM 未对齐访问
    if (out_uid) memcpy(out_uid, &resp.uid, sizeof(*out_uid));
    if (out_flags) memcpy(out_flags, &resp.flags, sizeof(*out_flags));

    int32_t code;
    memcpy(&code, &resp.code, sizeof(code));

    if (out_fd) {
        char cmsg_buf[CMSG_SPACE(sizeof(int))];
        struct iovec iov = { .iov_base = &resp, .iov_len = sizeof(resp) };
        struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cmsg_buf, .msg_controllen = sizeof(cmsg_buf) };
        recvmsg(g_sock, &msg, 0);
        *out_fd = CMSG_FIRSTHDR(&msg) ? *(int *)CMSG_DATA(CMSG_FIRSTHDR(&msg)) : -1;
    }
    return code;
}
