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

struct Group {
    string owner;
    set<string> members;
    set<string> pending;
};

struct User {
    string password;
    bool logged_in = false;
};

map<string, User> users;
map<string, Group> groups;
string missing_args = "ERROR missing_arguments";

void do_logout(string& current_user) {
    if (!current_user.empty()) {
        users[current_user].logged_in = false;
        current_user = "";
    }
}

string check_logged_in(const string& current_user) {
    if (current_user.empty()) return "ERROR not_logged_in";
    return "";
}

string check_group_exists(const string& group_id, map<string, Group>::iterator& out_it) {
    out_it = groups.find(group_id);
    if (out_it == groups.end()) return "ERROR group_not_found";
    return "";
}

string check_is_owner(const Group& g, const string& current_user) {
    if (g.owner != current_user) return "ERROR not_owner";
    return "";
}

string handle_command(const string& line, string& current_user) {
    istringstream iss(line);
    string tok1, tok2;
    iss >> tok1 >> tok2;

    if (tok1 == "create" && tok2 == "user") {
        string username, password;
        iss >> username >> password;
        if (username.empty() || password.empty()) return missing_args;
        if (users.count(username)) return "ERROR user_already_exists";
        users[username] = {password, false};
        return "SUCCESS user_created";
    }

    if (tok1 == "login") {
        string username = tok2, password;
        iss >> password;
        if (username.empty() || password.empty()) return missing_args;
        auto it = users.find(username);
        if (it == users.end()) return "ERROR user_not_found";
        if (it->second.password != password) return "ERROR wrong_password";
        if (it->second.logged_in) return "ERROR already_logged_in";
        it->second.logged_in = true;
        current_user = username;
        return "SUCCESS logged_in";
    }

    if (tok1 == "logout") {
        string res = check_logged_in(current_user);
        if (!res.empty()) return res;
        do_logout(current_user);
        return "SUCCESS logged_out";
    }

    if (tok1 == "create" && tok2 == "group") {
        string res = check_logged_in(current_user);
        if (!res.empty()) return res;

        string group_id;
        iss >> group_id;
        if (group_id.empty()) return missing_args;

        if (groups.count(group_id)) return "ERROR group_already_exists";

        Group g;
        g.owner = current_user;
        g.members.insert(current_user);
        groups[group_id] = g;
        return "SUCCESS group_created";
    }

    if (tok1 == "join" && tok2 == "group") {
        string res = check_logged_in(current_user);
        if (!res.empty()) return res;

        string group_id;
        iss >> group_id;
        if (group_id.empty()) return missing_args;

        map<string, Group>::iterator it;
        string err = check_group_exists(group_id, it);
        if (!err.empty()) return err;

        Group& g = it->second;
        if (g.members.count(current_user)) return "ERROR already_member";
        if (g.pending.count(current_user)) return "ERROR already_pending";
        g.pending.insert(current_user);
        return "SUCCESS join_requested";
    }

    if (tok1 == "list" && tok2 == "groups") {
        string res = check_logged_in(current_user);
        if (!res.empty()) return res;

        if (groups.empty()) return "SUCCESS no_groups";

        ostringstream oss;
        oss << "SUCCESS";
        for (const auto& [group_id, g] : groups) {
            oss << " " << group_id << "(" << g.owner << "," << g.members.size() << ")";
        }
        return oss.str();
    }

    if (tok1 == "list" && tok2 == "requests") {
        string res = check_logged_in(current_user);
        if (!res.empty()) return res;

        string group_id;
        iss >> group_id;
        if (group_id.empty()) return missing_args;

        map<string, Group>::iterator it;
        string err = check_group_exists(group_id, it);
        if (!err.empty()) return err;

        Group& g = it->second;
        err = check_is_owner(g, current_user);
        if (!err.empty()) return err;

        if (g.pending.empty()) return "SUCCESS no_pending_requests";

        ostringstream oss;
        oss << "SUCCESS";
        for (const auto& user : g.pending) {
            oss << " " << user;
        }
        return oss.str();
    }

    if (tok1 == "accept" && tok2 == "request") {
        string err = check_logged_in(current_user);
        if (!err.empty()) return err;

        string group_id, username;
        iss >> group_id >> username;
        if (group_id.empty() || username.empty()) return missing_args;

        map<string, Group>::iterator it;
        err = check_group_exists(group_id, it);
        if (!err.empty()) return err;

        Group& g = it->second;
        err = check_is_owner(g, current_user);
        if (!err.empty()) return err;

        if (!g.pending.count(username)) return "ERROR no_such_request";

        g.pending.erase(username);
        g.members.insert(username);
        return "SUCCESS request_accepted";
    }

    if (tok1 == "leave" && tok2 == "group") {
        string err = check_logged_in(current_user);
        if (!err.empty()) return err;
        string group_id;
        iss >> group_id;
        if (group_id.empty()) return missing_args;
        map<string, Group>::iterator it;
        err = check_group_exists(group_id, it);
        if (!err.empty()) return err;

        Group& g = it->second;
        if (!g.members.count(current_user)) return "ERROR not_a_member";
        if (current_user == g.owner) return "ERROR owner_cannot_leave";
        g.members.erase(current_user);
        return "SUCCESS left_group";
    }

    return "ERROR unknown_command";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s tracker_info.txt tracker_no\n", argv[0]);
        return 1;
    }
    int port = (atoi(argv[2]) == 1) ? 5000 : 5001;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket failed");
        return 1;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return 1;
    }
    if (listen(listen_fd, 5) < 0) {
        perror("listen failed");
        return 1;
    }
    printf("Tracker is listening on port %d...\n", port);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }
        printf("Client connected.\n");
        string current_user = "";  // fresh per-connection state, per client

        while (true) {
            string payload;
            if (!recv_message(client_fd, payload)) {
                printf("Client disconnected or error.\n");
                do_logout(current_user);
                break;
            }
            printf("Received: %s\n", payload.c_str());
            string response = handle_command(payload, current_user);
            printf("Responding %s\n", response.c_str());

            if (!send_message(client_fd, response)) {
                printf("Failed to send the response\n");
                do_logout(current_user);
                break;
            }
        }
        close(client_fd);
    }
    close(listen_fd);
    return 0;
}