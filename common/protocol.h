#pragma once
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>

inline bool send_all(int fd, const char* buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t n = send(fd,buf+total_sent,len-total_sent,0);
        if (n <= 0) return false;
        total_sent += n;
    }
    return true;
}
inline bool recv_all(int fd, char* buf, size_t len) {
    size_t total_received = 0;
     while (total_received < len) {
        ssize_t n = recv(fd, buf+total_received,len-total_received,0);
        if (n <= 0) return false;
        total_received += n;
    }
    return true;
}
inline bool send_message(int fd, const std::string& payload) {
    uint32_t len = htonl((uint32_t)payload.size());
    if (!send_all(fd, (const char*)&len, sizeof(len))) return false;
    return send_all(fd, payload.data(), payload.size());
}
inline bool recv_message(int fd, std::string& payload_out) {
    uint32_t len_net;
    if (!recv_all(fd, (char*)&len_net, sizeof(len_net))) return false;
    uint32_t len = ntohl(len_net);
    payload_out.resize(len);
    if (len == 0) return true;
    return recv_all(fd, &payload_out[0], len);
}