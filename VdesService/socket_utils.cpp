#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "socket_utils.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
static bool winsock_initialized = false;
#endif

bool init_sockets() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }
    winsock_initialized = true;
#endif
    return true;
}

void cleanup_sockets() {
#ifdef _WIN32
    if (winsock_initialized) WSACleanup();
#endif
}

socket_t create_udp_socket() {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock != SOCKET_INVALID) {
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    }
    return sock;
}

socket_t create_tcp_socket() {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock != SOCKET_INVALID) {
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    }
    return sock;
}

bool bind_socket(socket_t sock, const std::string& ip, uint16_t port) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE && ip != "0.0.0.0") {
        struct hostent* he = gethostbyname(ip.c_str());
        if (!he) return false;
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    return bind(sock, (sockaddr*)&addr, sizeof(addr)) == 0;
}

bool set_nonblocking(socket_t sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

int send_udp(socket_t sock, const void* data, size_t len, const std::string& ip, uint16_t port) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    return sendto(sock, (const char*)data, (int)len, 0, (sockaddr*)&addr, sizeof(addr));
}

int recv_udp(socket_t sock, void* buf, size_t buf_size, std::string& src_ip, uint16_t& src_port) {
    sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    int ret = recvfrom(sock, (char*)buf, (int)buf_size, 0, (sockaddr*)&src_addr, &addr_len);
    if (ret > 0) {
        src_ip = inet_ntoa(src_addr.sin_addr);
        src_port = ntohs(src_addr.sin_port);
    }
    return ret;
}

bool listen_tcp(socket_t sock, int backlog) {
    return listen(sock, backlog) == 0;
}

socket_t accept_tcp(socket_t listen_sock, std::string& client_ip, uint16_t& client_port) {
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    socket_t client = accept(listen_sock, (sockaddr*)&client_addr, &addr_len);
    if (client != SOCKET_INVALID) {
        client_ip = inet_ntoa(client_addr.sin_addr);
        client_port = ntohs(client_addr.sin_port);
    }
    return client;
}

ssize_t recv_all(socket_t sock, void* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t ret = recv(sock, (char*)buf + received, (int)(len - received), 0);
        if (ret <= 0) return ret;
        received += ret;
    }
    return (ssize_t)received;
}

ssize_t send_all(socket_t sock, const void* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t ret = send(sock, (const char*)buf + sent, (int)(len - sent), 0);
        if (ret <= 0) return ret;
        sent += ret;
    }
    return (ssize_t)sent;
}