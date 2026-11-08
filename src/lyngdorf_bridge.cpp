// lyngdorf_bridge.cpp
//
// TCP <-> CP210x (USB-RS232) bridge for the Lyngdorf TDAi-2170.
// Replaces a serial device server such as a Moxa NPort in "Reverse Telnet"
// mode: the Lyngdorf Remote app connects to <ip>:4001 and every byte is passed
// unchanged to and from the amplifier's RS232 port.
// Talks to the CP210x chip directly through libusb, so no kernel driver
// (cp210x / usbserial) is needed.
//
// Fully event-driven, no polling:
//  - One asynchronous bulk-IN transfer without a timeout is always pending.
//    The USB host controller polls the device in hardware; the CPU only gets
//    an interrupt when the CP210x actually has data.
//  - The program sleeps in epoll_wait() without a timeout, waiting on the
//    listening socket, client sockets, signals (signalfd) and libusb's own
//    file descriptors.
//  - When idle: zero wake-ups, zero CPU.
//
// Serial settings per the Lyngdorf external control documentation:
// 115200 baud, 8N1, no flow control.
//
// Build:  make            (or see the Makefile for the plain compiler command)
//
// Usage:  lyngdorf_bridge [-p tcp-port] [-l listen-address] [-b baud] [-d vid:pid]
//         defaults:       -p 4001       -l 0.0.0.0          -b 115200 -d 10c4:ea60
//
// SPDX-License-Identifier: MIT

#include <libusb.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

// ---- CP210x vendor requests (Silicon Labs AN571) ---------------------------
constexpr uint8_t REQTYPE_HOST_TO_INTERFACE = 0x41;
constexpr uint8_t CP210X_IFC_ENABLE   = 0x00;
constexpr uint8_t CP210X_SET_LINE_CTL = 0x03;
constexpr uint8_t CP210X_SET_MHS      = 0x07;
constexpr uint8_t CP210X_SET_BAUDRATE = 0x1E;
constexpr unsigned CTRL_TIMEOUT_MS    = 1000;
constexpr unsigned OUT_TIMEOUT_MS     = 1000;
constexpr int      IN_BUF_SIZE        = 256;   // multiple of wMaxPacketSize (64)

struct Config {
    uint16_t    vid  = 0x10C4;
    uint16_t    pid  = 0xEA60;
    uint32_t    baud = 115200;
    uint16_t    port = 4001;
    const char* bind = "0.0.0.0";
};

enum class FdType { Listen, Client, Signal, Usb };

// ---- global state (single thread, so no locks needed) ----------------------
libusb_context*       g_ctx         = nullptr;
libusb_device_handle* g_dev         = nullptr;
uint8_t               g_ep_in       = 0;
uint8_t               g_ep_out      = 0;
int                   g_epfd        = -1;
libusb_transfer*      g_in_xfer     = nullptr;
unsigned char         g_in_buf[IN_BUF_SIZE];
bool                  g_in_active   = false;
int                   g_out_pending = 0;
bool                  g_running     = true;
int                   g_exit_code   = 0;

std::unordered_map<int, FdType> g_fds;
std::set<int>                   g_clients;
std::vector<int>                g_dead_clients;   // closed after the current event

void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log(const char* fmt, ...) {
    char ts[32];
    time_t now = time(nullptr);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(stdout, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

const char* xfer_status_name(libusb_transfer_status s) {
    switch (s) {
        case LIBUSB_TRANSFER_COMPLETED: return "COMPLETED";
        case LIBUSB_TRANSFER_ERROR:     return "ERROR";
        case LIBUSB_TRANSFER_TIMED_OUT: return "TIMED_OUT";
        case LIBUSB_TRANSFER_CANCELLED: return "CANCELLED";
        case LIBUSB_TRANSFER_STALL:     return "STALL";
        case LIBUSB_TRANSFER_NO_DEVICE: return "NO_DEVICE";
        case LIBUSB_TRANSFER_OVERFLOW:  return "OVERFLOW";
    }
    return "?";
}

void fatal_stop(int code) {
    if (g_running) {
        g_running   = false;
        g_exit_code = code;
    }
}

// ---- epoll helpers ----------------------------------------------------------
bool epoll_add(int fd, uint32_t events, FdType type) {
    epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;
    if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        log("epoll_ctl ADD fd %d: %s", fd, strerror(errno));
        return false;
    }
    g_fds[fd] = type;
    return true;
}

void epoll_del(int fd) {
    epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, nullptr);
    g_fds.erase(fd);
}

// libusb tells us which file descriptors to watch (usbfs, eventfd, timerfd)
void LIBUSB_CALL usb_fd_added(int fd, short events, void*) {
    uint32_t ev = 0;
    if (events & POLLIN)  ev |= EPOLLIN;
    if (events & POLLOUT) ev |= EPOLLOUT;   // usbfs: "a transfer has completed"
    epoll_add(fd, ev, FdType::Usb);
}

void LIBUSB_CALL usb_fd_removed(int fd, void*) { epoll_del(fd); }

// ---- clients ----------------------------------------------------------------
void close_client(int fd) {
    if (!g_clients.erase(fd)) return;
    epoll_del(fd);
    sockaddr_in a{};
    socklen_t   len = sizeof a;
    char        ip[INET_ADDRSTRLEN] = "?";
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&a), &len) == 0)
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    close(fd);
    log("Client disconnected: %s:%u (fd %d)", ip, ntohs(a.sin_port), fd);
}

void flush_dead_clients() {
    for (int fd : g_dead_clients) close_client(fd);
    g_dead_clients.clear();
}

void broadcast(const unsigned char* data, int len) {
    for (int fd : g_clients) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n != len) g_dead_clients.push_back(fd);   // gone or not reading
    }
}

// ---- USB receive (amplifier -> clients) -------------------------------------
void LIBUSB_CALL in_cb(libusb_transfer* t) {
    switch (t->status) {
        case LIBUSB_TRANSFER_COMPLETED:
        case LIBUSB_TRANSFER_TIMED_OUT:
            if (t->actual_length > 0) broadcast(t->buffer, t->actual_length);
            if (g_running) {
                int r = libusb_submit_transfer(t);
                if (r == 0) return;                       // stays pending
                log("USB: resubmitting IN transfer failed: %s", libusb_error_name(r));
                fatal_stop(1);
            }
            break;
        case LIBUSB_TRANSFER_CANCELLED:
            break;                                        // normal during shutdown
        default:
            log("USB receive error: %s", xfer_status_name(t->status));
            fatal_stop(1);                                // let the supervisor restart us
            break;
    }
    g_in_active = false;
}

bool start_in_transfer() {
    g_in_xfer = libusb_alloc_transfer(0);
    if (!g_in_xfer) return false;
    // timeout 0 = infinite: the transfer waits passively until data arrives
    libusb_fill_bulk_transfer(g_in_xfer, g_dev, g_ep_in, g_in_buf, IN_BUF_SIZE,
                              in_cb, nullptr, 0);
    int r = libusb_submit_transfer(g_in_xfer);
    if (r != 0) {
        log("USB: starting IN transfer failed: %s", libusb_error_name(r));
        return false;
    }
    g_in_active = true;
    return true;
}

// ---- USB send (clients -> amplifier) ----------------------------------------
void LIBUSB_CALL out_cb(libusb_transfer* t) {
    --g_out_pending;
    if (t->status != LIBUSB_TRANSFER_COMPLETED && g_running) {
        log("USB send error: %s", xfer_status_name(t->status));
        if (t->status == LIBUSB_TRANSFER_NO_DEVICE) fatal_stop(1);
    }
    // buffer and transfer are freed by libusb (FREE_* flags)
}

void usb_write(const unsigned char* data, int len) {
    libusb_transfer* t   = libusb_alloc_transfer(0);
    auto*            buf = static_cast<unsigned char*>(malloc(len));
    if (!t || !buf) {
        free(buf);
        if (t) libusb_free_transfer(t);
        log("Out of memory while sending");
        return;
    }
    memcpy(buf, data, len);
    libusb_fill_bulk_transfer(t, g_dev, g_ep_out, buf, len, out_cb, nullptr,
                              OUT_TIMEOUT_MS);
    t->flags = LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
    int r = libusb_submit_transfer(t);
    if (r != 0) {
        log("USB: send failed: %s", libusb_error_name(r));
        libusb_free_transfer(t);                         // also frees buf
        if (r == LIBUSB_ERROR_NO_DEVICE) fatal_stop(1);
        return;
    }
    ++g_out_pending;
}

// ---- USB: open and configure the CP210x -------------------------------------
bool cp210x_ctrl(uint8_t req, uint16_t value, unsigned char* data, uint16_t len) {
    int r = libusb_control_transfer(g_dev, REQTYPE_HOST_TO_INTERFACE, req, value,
                                    0 /* interface 0 */, data, len, CTRL_TIMEOUT_MS);
    if (r < 0) {
        log("CP210x request 0x%02x failed: %s", req, libusb_error_name(r));
        return false;
    }
    return true;
}

bool open_cp210x(const Config& cfg) {
    g_dev = libusb_open_device_with_vid_pid(g_ctx, cfg.vid, cfg.pid);
    if (!g_dev) {
        log("CP210x %04x:%04x not found or not accessible (check lsusb and permissions)",
            cfg.vid, cfg.pid);
        return false;
    }
    libusb_set_auto_detach_kernel_driver(g_dev, 1);   // in case cp210x.ko is loaded
    int r = libusb_claim_interface(g_dev, 0);
    if (r != 0) {
        log("Claiming interface failed: %s (is another bridge or ser2net running?)",
            libusb_error_name(r));
        return false;
    }

    // Bulk endpoints from the descriptor (CP2102: 0x01/0x81, CP2102N: 0x02/0x82)
    libusb_config_descriptor* cd = nullptr;
    r = libusb_get_active_config_descriptor(libusb_get_device(g_dev), &cd);
    if (r != 0) {
        log("Reading config descriptor failed: %s", libusb_error_name(r));
        return false;
    }
    const libusb_interface_descriptor& alt = cd->interface[0].altsetting[0];
    for (int i = 0; i < alt.bNumEndpoints; ++i) {
        const libusb_endpoint_descriptor& ep = alt.endpoint[i];
        if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
            continue;
        if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) g_ep_in = ep.bEndpointAddress;
        else                                          g_ep_out = ep.bEndpointAddress;
    }
    libusb_free_config_descriptor(cd);
    if (!g_ep_in || !g_ep_out) {
        log("No bulk endpoints found");
        return false;
    }
    log("Endpoints: OUT=0x%02x IN=0x%02x", g_ep_out, g_ep_in);

    unsigned char baud[4] = {
        static_cast<unsigned char>(cfg.baud),
        static_cast<unsigned char>(cfg.baud >> 8),
        static_cast<unsigned char>(cfg.baud >> 16),
        static_cast<unsigned char>(cfg.baud >> 24),
    };
    return cp210x_ctrl(CP210X_IFC_ENABLE, 0x0001, nullptr, 0) &&
           cp210x_ctrl(CP210X_SET_BAUDRATE, 0, baud, sizeof baud) &&
           cp210x_ctrl(CP210X_SET_LINE_CTL, 0x0800, nullptr, 0) &&  // 8N1
           cp210x_ctrl(CP210X_SET_MHS, 0x0303, nullptr, 0);         // DTR+RTS high
}

// ---- network ----------------------------------------------------------------
int open_listen_socket(const Config& cfg) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) { log("socket: %s", strerror(errno)); return -1; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons(cfg.port);
    if (inet_pton(AF_INET, cfg.bind, &a.sin_addr) != 1) {
        log("Invalid listen address: %s", cfg.bind);
        close(fd);
        return -1;
    }
    if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || listen(fd, 8) < 0) {
        log("bind/listen %s:%u: %s", cfg.bind, cfg.port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

void accept_clients(int lfd) {
    for (;;) {
        sockaddr_in a{};
        socklen_t   len = sizeof a;
        int fd = accept4(lfd, reinterpret_cast<sockaddr*>(&a), &len,
                         SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                log("accept: %s", strerror(errno));
            return;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        if (!epoll_add(fd, EPOLLIN | EPOLLRDHUP, FdType::Client)) {
            close(fd);
            continue;
        }
        g_clients.insert(fd);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        log("Client connected: %s:%u (fd %d)", ip, ntohs(a.sin_port), fd);
    }
}

void read_client(int fd) {
    unsigned char buf[512];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n > 0) { usb_write(buf, static_cast<int>(n)); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n < 0 && errno == EINTR) continue;
        g_dead_clients.push_back(fd);                     // n == 0 or error
        return;
    }
}

// ---- arguments --------------------------------------------------------------
void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [-p tcp-port] [-l listen-address] [-b baud] [-d vid:pid]\n"
            "Defaults: -p 4001 -l 0.0.0.0 -b 115200 -d 10c4:ea60\n", prog);
}

bool parse_args(int argc, char** argv, Config& cfg) {
    int c;
    while ((c = getopt(argc, argv, "p:l:b:d:h")) != -1) {
        switch (c) {
            case 'p': cfg.port = static_cast<uint16_t>(strtoul(optarg, nullptr, 10)); break;
            case 'l': cfg.bind = optarg; break;
            case 'b': cfg.baud = static_cast<uint32_t>(strtoul(optarg, nullptr, 10)); break;
            case 'd': {
                unsigned v, p;
                if (sscanf(optarg, "%x:%x", &v, &p) != 2) { usage(argv[0]); return false; }
                cfg.vid = static_cast<uint16_t>(v);
                cfg.pid = static_cast<uint16_t>(p);
                break;
            }
            default: usage(argv[0]); return false;
        }
    }
    if (cfg.port == 0 || cfg.baud == 0) { usage(argv[0]); return false; }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 2;

    // Signals via signalfd: SIGINT (Ctrl+C) and SIGTERM (service stop)
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    signal(SIGPIPE, SIG_IGN);

    g_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (g_epfd < 0) { log("epoll_create1: %s", strerror(errno)); return 1; }

    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0 || !epoll_add(sfd, EPOLLIN, FdType::Signal)) return 1;

    int r = libusb_init(&g_ctx);
    if (r != 0) { log("libusb_init: %s", libusb_error_name(r)); return 1; }

    if (!open_cp210x(cfg)) {
        if (g_dev) libusb_close(g_dev);
        libusb_exit(g_ctx);
        return 1;
    }

    // Add libusb's file descriptors to our epoll set (now and when they change)
    const libusb_pollfd** pfds = libusb_get_pollfds(g_ctx);
    if (pfds) {
        for (int i = 0; pfds[i]; ++i) usb_fd_added(pfds[i]->fd, pfds[i]->events, nullptr);
        libusb_free_pollfds(pfds);
    }
    libusb_set_pollfd_notifiers(g_ctx, usb_fd_added, usb_fd_removed, nullptr);
    // On Linux libusb handles its timeouts with a timerfd; otherwise track them here
    const bool usb_fd_timeouts = libusb_pollfds_handle_timeouts(g_ctx) != 0;

    int lfd = open_listen_socket(cfg);
    if (lfd < 0 || !epoll_add(lfd, EPOLLIN, FdType::Listen) || !start_in_transfer()) {
        g_running   = false;
        g_exit_code = 1;
    } else {
        log("Bridge listening on %s:%u -> CP210x @ %u 8N1", cfg.bind, cfg.port, cfg.baud);
    }

    // ---- event loop: sleeps in epoll_wait until something actually happens --
    while (g_running) {
        int timeout_ms = -1;
        if (!usb_fd_timeouts) {
            timeval tv{};
            if (libusb_get_next_timeout(g_ctx, &tv) == 1)
                timeout_ms = static_cast<int>(tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000);
        }

        epoll_event evs[16];
        int n = epoll_wait(g_epfd, evs, 16, timeout_ms);
        if (n < 0) {
            if (errno == EINTR) continue;
            log("epoll_wait: %s", strerror(errno));
            fatal_stop(1);
            break;
        }

        bool usb_ready = (n == 0 && !usb_fd_timeouts);
        for (int i = 0; i < n; ++i) {
            int  fd = evs[i].data.fd;
            auto it = g_fds.find(fd);
            if (it == g_fds.end()) continue;
            switch (it->second) {
                case FdType::Listen: accept_clients(fd); break;
                case FdType::Client:
                    if (evs[i].events & (EPOLLERR | EPOLLHUP)) g_dead_clients.push_back(fd);
                    else read_client(fd);
                    break;
                case FdType::Signal: {
                    signalfd_siginfo si;
                    while (read(fd, &si, sizeof si) == sizeof si) {}
                    log("Signal %u received, stopping bridge...", si.ssi_signo);
                    g_running = false;
                    break;
                }
                case FdType::Usb: usb_ready = true; break;
            }
        }
        if (usb_ready) {
            timeval zero{0, 0};
            libusb_handle_events_timeout_completed(g_ctx, &zero, nullptr);
        }
        flush_dead_clients();
    }

    // ---- clean shutdown -----------------------------------------------------
    if (g_in_active) libusb_cancel_transfer(g_in_xfer);
    for (int i = 0; i < 20 && (g_in_active || g_out_pending > 0); ++i) {
        timeval tv{0, 50000};                             // max ~1 s in total
        libusb_handle_events_timeout_completed(g_ctx, &tv, nullptr);
    }
    libusb_set_pollfd_notifiers(g_ctx, nullptr, nullptr, nullptr);

    for (int fd : std::vector<int>(g_clients.begin(), g_clients.end())) close_client(fd);
    if (lfd >= 0) close(lfd);
    if (g_in_xfer && !g_in_active) libusb_free_transfer(g_in_xfer);
    libusb_release_interface(g_dev, 0);
    libusb_close(g_dev);
    libusb_exit(g_ctx);
    close(sfd);
    close(g_epfd);

    log("Bridge stopped (exit %d).", g_exit_code);
    return g_exit_code;
}
