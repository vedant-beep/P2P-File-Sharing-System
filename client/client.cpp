#include "../common/protocol.h"
#include<bits/stdc++.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <IP>:<PORT>\n", argv[0]);
        return 1;
    }

    char ip[64];
    int port;
    sscanf(argv[1], "%63[^:]:%d", ip, &port);
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket failed");
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address\n");
        return 1;
    }
    if (connect(sock_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        return 1;
    }

    printf("Connected to tracker at %s:%d\n", ip, port);
    send_message(sock_fd, "HELLO");
    printf("Sent: HELLO\n");

    string response;
    if(recv_message(sock_fd,response)){
        printf("Received: %s\n",response.c_str());
    }
    else{
        printf("No response/ connection closed.\n");
    }

    close(sock_fd);
    return 0;
}