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

struct Group{
    string owner;
    set<string>members;
    set<string>pending;
};

struct User{
    string password;
    bool logged_in = false;
};

map<string,User>users;
map<string,Group>groups;

int main(int argc, char* argv[]){
    // TEMPORARY — delete before Phase 4
    users["alice"] = {"password123", false};
    groups["G1"] = {"alice", {"alice"}, {}};
    groups["G1"].pending.insert("bob");

    printf("User alice exists: %d\n", users.count("alice") > 0);
    printf("Group G1 owner: %s\n", groups["G1"].owner.c_str());
    printf("Group G1 member count: %zu\n", groups["G1"].members.size());
    printf("Is bob pending in G1: %d\n", groups["G1"].pending.count("bob") > 0);

    groups["G1"].pending.erase("bob");
    groups["G1"].members.insert("bob");
    printf("After accept — bob is member: %d, bob is pending: %d\n",
        groups["G1"].members.count("bob") > 0,
        groups["G1"].pending.count("bob") > 0);
return 0; // stop here for this test, before the real socket code runs
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

    string payload;
    if(!recv_message(client_fd,payload)){
        printf("Client disconnected or error .\n");
        close(client_fd);
        close(listen_fd);
        return 1;
    }
    printf("Received: %s\n", payload.c_str());
    send_message(client_fd, "HELLO_ACK");
     close(client_fd);
    close(listen_fd);
    return 0;
}