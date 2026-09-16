#include "../common/protocol.h"
#include<bits/stdc++.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>

using namespace std;

struct TrackerAddr {
    string ip;
    int port;
};

vector<TrackerAddr> read_tracker_info(const string& path) {
    vector<TrackerAddr> addrs;
    ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "Warning: could not open %s, no failover trackers known\n", path.c_str());
        return addrs;
    }
    string ip;
    int port;
    while (file >> ip >> port) {
        addrs.push_back({ip, port});
    }
    return addrs;
}

int connect_to_tracker(const string& ip, int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        close(sock_fd);
        return -1;
    }
    if (connect(sock_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock_fd);
        return -1;
    }
    return sock_fd;
}

// Tries the preferred tracker first; on failure, tries every other tracker
// listed in tracker_info.txt. Returns a connected fd, or -1 if none reachable.
int connect_with_failover(const string& preferred_ip, int preferred_port,
                           const vector<TrackerAddr>& all_trackers) {
    int fd = connect_to_tracker(preferred_ip, preferred_port);
    if (fd >= 0) {
        printf("Connected to tracker at %s:%d\n", preferred_ip.c_str(), preferred_port);
        return fd;
    }
    printf("Could not reach %s:%d, trying other known trackers...\n", preferred_ip.c_str(), preferred_port);

    for (const auto& t : all_trackers) {
        if (t.ip == preferred_ip && t.port == preferred_port) continue; // already tried
        fd = connect_to_tracker(t.ip, t.port);
        if (fd >= 0) {
            printf("Connected to tracker at %s:%d\n", t.ip.c_str(), t.port);
            return fd;
        }
        printf("Could not reach %s:%d\n", t.ip.c_str(), t.port);
    }
    return -1;
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <IP>:<PORT> tracker_info.txt\n", argv[0]);
        return 1;
    }

    char ip_buf[64];
    int port;
    sscanf(argv[1], "%63[^:]:%d", ip_buf, &port);
    string preferred_ip = ip_buf;
    int preferred_port = port;

    vector<TrackerAddr> all_trackers = read_tracker_info(argv[2]);

    int sock_fd = connect_with_failover(preferred_ip, preferred_port, all_trackers);
    if (sock_fd < 0) {
        fprintf(stderr, "Could not connect to any known tracker. Exiting.\n");
        return 1;
    }

    printf("Type commands (Ctrl+D to quit):\n");

    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;

        bool ok = send_message(sock_fd, line);
        string response;
        if (ok) ok = recv_message(sock_fd, response);

        if (!ok) {
            printf("Connection to tracker lost. Attempting to fail over...\n");
            close(sock_fd);
            sock_fd = connect_with_failover(preferred_ip, preferred_port, all_trackers);
            if (sock_fd < 0) {
                printf("All known trackers are unreachable. Exiting.\n");
                break;
            }
            printf("Reconnected. Your previous command was NOT resent — please retry it. You will also need to 'login' again, since sessions are per-connection.\n");
            continue;
        }

        printf("%s\n", response.c_str());
    }
    close(sock_fd);
    return 0;
}