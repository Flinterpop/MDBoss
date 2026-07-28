// A real loopback listening socket, shared by both spike harnesses.
//
// "Was the request blocked?" is answered by whether a TCP connection ever
// arrived -- never by trusting the filter's own bookkeeping, which is exactly
// the mistake that would make a broken lock look like a working one.  Both the
// wxWidgets harness and the bare Win32 one use this same class so their
// results are directly comparable.

#ifndef MDBOSS_SPIKE_EGRESS_PROBE_H
#define MDBOSS_SPIKE_EGRESS_PROBE_H

#include <atomic>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

class EgressProbe {
public:
    bool start()
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == INVALID_SOCKET) {
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;   // let the OS choose
        if (bind(listener_, reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr)) != 0) {
            return false;
        }
        int len = sizeof(addr);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&addr),
                        &len) != 0) {
            return false;
        }
        port_ = ntohs(addr.sin_port);
        if (listen(listener_, 8) != 0) {
            return false;
        }
        thread_ = std::thread([this] { accept_loop(); });
        return true;
    }

    void stop()
    {
        running_ = false;
        if (listener_ != INVALID_SOCKET) {
            closesocket(listener_);
            listener_ = INVALID_SOCKET;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        WSACleanup();
    }

    unsigned short port() const { return port_; }
    bool was_contacted() const { return contacted_.load(); }

private:
    void accept_loop()
    {
        while (running_.load()) {
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                return;   // closed by stop(), or a real error
            }
            contacted_ = true;
            closesocket(client);
        }
    }

    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{true};
    std::atomic<bool> contacted_{false};
};

#endif  // MDBOSS_SPIKE_EGRESS_PROBE_H
