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
string current_user = "";

void do_logout(string& current_user){
    if(!current_user.empty()){
        users[current_user].logged_in = false;
        current_user = "";
    }
}

string handle_command(const string &line,string& current_user){
    istringstream iss(line);
    string tok1,tok2;
    iss>>tok1>>tok2;
    if(tok1 == "create" && tok2 == "user"){
        string username,password;
        iss>>username>>password;
        if(username.empty() || password.empty()){
            return "ERROR missing arguments";
        }
        if(users.count(username)){
            return "ERROR user already exists";
        }
        users[username] = {password,false};
        return "SUCCESS user created";
    }

    if(tok1 == "login"){
        string username = tok2,password;
        iss>>password;
        if(username.empty() || password.empty()) return "ERROR missing arguments";
        auto it = users.find(username);
        if(it == users.end()) return "ERROR user not found";
        if(it->second.password != password) return "ERROR wrong password";
        if(it->second.logged_in) return "ERROR already logged in";
        it->second.logged_in = true;
        current_user = username;
        return "SUCCESS logged in";
    }

    if(tok1 == "logout"){
        if(current_user.empty()) return "ERROR not logged in";
        do_logout(current_user);
        return "SUCCESS logged out";
    }

    if(tok1 == "create" && tok2 == "group"){
        if(current_user.empty() ) return "ERROR not logged in";

        string group_id;
        iss>>group_id;

        if(group_id.empty()) return "ERROR missing arguments";
        if (groups.count(group_id)) return "ERROR group_already_exists";
        Group g;
        g.owner = current_user;
        g.members.insert(current_user);
        groups[group_id] = g;
        return "SUCCESS group_created";
    }
    return "ERROR unknown command";
}


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

    while(true){
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }
        printf("Client connected.\n");

        while(true){
            string payload;
            if(!recv_message(client_fd,payload)){
                printf("Client disconnected or error .\n");
                do_logout(current_user);
                break;
            }
            printf("Received: %s\n", payload.c_str());
            string response = handle_command(payload,current_user);
            printf("Responding %s\n",response.c_str());
            
            if(!send_message(client_fd,response)){
                printf("Failed to see the response\n");
                do_logout(current_user);
                break;
            }
        }
        close(client_fd);
    }
    close(listen_fd);
    return 0;
}