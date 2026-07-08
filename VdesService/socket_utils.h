#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define SOCKET_INVALID INVALID_SOCKET
#define SOCKET_ERROR_RETURN SOCKET_ERROR
#define close_socket closesocket
#ifdef _MSC_VER
#include <stddef.h>
typedef ptrdiff_t ssize_t;
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
typedef int socket_t;
#define SOCKET_INVALID (-1)
#define SOCKET_ERROR_RETURN (-1)
#define close_socket close
#endif

#include <string>
#include <vector>

bool init_sockets();
void cleanup_sockets();
socket_t create_udp_socket();
socket_t create_tcp_socket();
bool bind_socket(socket_t sock, const std::string& ip, uint16_t port);
bool set_nonblocking(socket_t sock);
int send_udp(socket_t sock, const void* data, size_t len, const std::string& ip, uint16_t port);
int recv_udp(socket_t sock, void* buf, size_t buf_size, std::string& src_ip, uint16_t& src_port);
socket_t accept_tcp(socket_t listen_sock, std::string& client_ip, uint16_t& client_port);
bool listen_tcp(socket_t sock, int backlog);
ssize_t recv_all(socket_t sock, void* buf, size_t len);
ssize_t send_all(socket_t sock, const void* buf, size_t len);

#endif