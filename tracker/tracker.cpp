#include<bits/stdc++.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;


int main(int argc, char* argv[]){
     if (argc<3) {
        fprintf(stderr, "Usage: %s tracker_info.txt tracker_no\n", argv[0]);
        return 1;
    }
    int port = (atoi(argv[2]) == 1)?5000:5001;
    int listen_fd = socket(AF_INET, SOCK_STREAM,0);
    if(listen_fd < 0){
        perror("Socket failed");
        return 1;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if(bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0){
        perror("bind failed");
        return 1;

    }
    if(listen(listen_fd,5) < 0){
        perror("listen failed");
        return 1;
    }
    printf("Tracker is listening on port %d...\n",port);
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept failed");
        return 1;
    }
    printf("Client connected.\n");
    char buf[1024] = {0};
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        printf("Client disconnected or error.\n");
        close(client_fd);
        close(listen_fd);
        return 1;
    }
    buf[n] = '\0';
    printf("Received: %s\n", buf);
    const char* response = "HELLO_ACK";
    send(client_fd, response, strlen(response), 0);
     close(client_fd);
    close(listen_fd);
    return 0;
}