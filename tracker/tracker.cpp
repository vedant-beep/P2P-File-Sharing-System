#include "../common/protocol.h"
#include<bits/stdc++.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <csignal>
using namespace std;

int g_sync_fd = -1;

struct Group {
    string owner;
    set<string> members;
    set<string> pending;
};

struct User {
    string password;
    bool logged_in = false;
};

struct TrackerAddr {
    string ip;
    int port;
};

int g_tracker_no = 0;
long long g_start_epoch = 0;
set<string> g_applied_ops;
vector<string> g_op_log;

map<string, User> users;
map<string, Group> groups;
mutex g_state_mutex;
mutex g_sync_send_mutex; 
string missing_args = "ERROR missing_arguments";

bool sync_send(const string& line) {
    lock_guard<mutex> lock(g_sync_send_mutex);
    if (g_sync_fd < 0) return false;
    return send_message(g_sync_fd, line);
}

string next_op_id() {
    static int counter = 0;
    counter++;
    return "T" + to_string(g_tracker_no) + "-" + to_string(g_start_epoch) + "-" + to_string(counter);
}

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

void forward_sync_op(const string& payload) {
    string op_id = next_op_id();
    string op_line = "SYNC_OP " + op_id+ " " + payload;
    g_applied_ops.insert(op_id);
    g_op_log.push_back(op_line);
    printf("[sync] op log size now %zu\n", g_op_log.size());
    if (g_sync_fd < 0) {
        printf("[sync] no peer connected, skipping forward: %s\n", op_line.c_str());
        return;
    }
    if (!sync_send(op_line)) {
        printf("[sync] forward failed (peer likely down or not connected): %s\n", op_line.c_str());
    }
}

// ---------------------------------------------------------------------
// Raw state mutations. Caller MUST already hold g_state_mutex.
// No validation, no locking, no forwarding — just the mechanical change.
// Shared by handle_command (client path) and apply_sync_op (replay path)
// so the actual mutation logic never has to be written/fixed twice.
// ---------------------------------------------------------------------

void raw_create_user(const string& username, const string& password) {
    users[username] = {password, false};
}

void raw_create_group(const string& group_id, const string& owner) {
    Group g;
    g.owner = owner;
    g.members.insert(owner);
    groups[group_id] = g;
}

void raw_join_group(const string& group_id, const string& username) {
    groups[group_id].pending.insert(username);
}

void raw_accept_request(const string& group_id, const string& username) {
    Group& g = groups[group_id];
    g.pending.erase(username);
    g.members.insert(username);
}

void raw_leave_group(const string& group_id, const string& username) {
    groups[group_id].members.erase(username);
}

string handle_command(const string& line, string& current_user) {
    lock_guard<mutex> lock(g_state_mutex);
    istringstream iss(line);
    string tok1, tok2;
    iss >> tok1 >> tok2;

    if (tok1 == "create" && tok2 == "user") {
        string username, password;
        iss >> username >> password;
        if (username.empty() || password.empty()) return missing_args;
        if (users.count(username)) return "ERROR user_already_exists";
        raw_create_user(username, password);
        forward_sync_op("CREATE_USER " + username + " " + password);
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

        raw_create_group(group_id, current_user);
        forward_sync_op("CREATE_GROUP " + group_id + " " + current_user);
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
        raw_join_group(group_id, current_user);
        forward_sync_op("JOIN_GROUP " + group_id + " " + current_user);
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

        raw_accept_request(group_id, username);
        forward_sync_op("ACCEPT_REQUEST " + group_id + " " + username);
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
        raw_leave_group(group_id, current_user);
        forward_sync_op("LEAVE_GROUP " + group_id + " " + current_user);
        return "SUCCESS left_group";
    }

    return "ERROR unknown_command";
}

void handle_client(int client_fd){
    printf("Client connected.\n");
        string current_user = "";

    while (true) {
        string payload;
        if (!recv_message(client_fd, payload)) {
            printf("Client disconnected or error.\n");
            {
                lock_guard<mutex> lock(g_state_mutex);
                do_logout(current_user);
            }
            break;
        }
        printf("Received: %s\n", payload.c_str());
        string response = handle_command(payload, current_user);
        printf("Responding %s\n", response.c_str());

        if (!send_message(client_fd, response)) {
            printf("Failed to send the response\n");
            {
                lock_guard<mutex> lock(g_state_mutex);
                do_logout(current_user);
            }
            break;
        }
    }
    close(client_fd);
}

void apply_sync_op(const string& op_line) {
    istringstream iss(op_line);
    string tag,op_id, opname;
    iss >> tag >>op_id>> opname;   // tag == "SYNC_OP"

    if (g_applied_ops.count(op_id)) {
        printf("[sync] duplicate op %s, ignoring\n", op_id.c_str());
        return;
    }

    g_applied_ops.insert(op_id);
    g_op_log.push_back(op_line);
    printf("[sync] op log size now %zu\n", g_op_log.size());
    
    if (opname == "CREATE_USER") {
        string username, password;
        iss >> username >> password;
        if (username.empty() || password.empty()) {
            printf("[sync] malformed CREATE_USER op, ignoring\n");
            return;
        }
        lock_guard<mutex> lock(g_state_mutex);
        if (users.count(username)) {
            printf("[sync] user '%s' already exists locally, skipping\n", username.c_str());
            return;
        }
        raw_create_user(username, password);
        printf("[sync] applied CREATE_USER %s\n", username.c_str());
        return;
    }

    if (opname == "CREATE_GROUP") {
        string group_id, owner;
        iss >> group_id >> owner;
        if (group_id.empty() || owner.empty()) {
            printf("[sync] malformed CREATE_GROUP op, ignoring\n");
            return;
        }
        lock_guard<mutex> lock(g_state_mutex);
        if (groups.count(group_id)) {
            printf("[sync] group '%s' already exists locally, skipping\n", group_id.c_str());
            return;
        }
        raw_create_group(group_id, owner);
        printf("[sync] applied CREATE_GROUP %s owner=%s\n", group_id.c_str(), owner.c_str());
        return;
    }

    if (opname == "JOIN_GROUP") {
        string group_id, username;
        iss >> group_id >> username;
        if (group_id.empty() || username.empty()) {
            printf("[sync] malformed JOIN_GROUP op, ignoring\n");
            return;
        }
        lock_guard<mutex> lock(g_state_mutex);
        if (!groups.count(group_id)) {
            printf("[sync] JOIN_GROUP for unknown group '%s', ignoring\n", group_id.c_str());
            return;
        }
        raw_join_group(group_id, username);
        printf("[sync] applied JOIN_GROUP %s %s\n", group_id.c_str(), username.c_str());
        return;
    }

    if (opname == "ACCEPT_REQUEST") {
        string group_id, username;
        iss >> group_id >> username;
        if (group_id.empty() || username.empty()) {
            printf("[sync] malformed ACCEPT_REQUEST op, ignoring\n");
            return;
        }
        lock_guard<mutex> lock(g_state_mutex);
        if (!groups.count(group_id)) {
            printf("[sync] ACCEPT_REQUEST for unknown group '%s', ignoring\n", group_id.c_str());
            return;
        }
        raw_accept_request(group_id, username);
        printf("[sync] applied ACCEPT_REQUEST %s %s\n", group_id.c_str(), username.c_str());
        return;
    }

    if (opname == "LEAVE_GROUP") {
        string group_id, username;
        iss >> group_id >> username;
        if (group_id.empty() || username.empty()) {
            printf("[sync] malformed LEAVE_GROUP op, ignoring\n");
            return;
        }
        lock_guard<mutex> lock(g_state_mutex);
        if (!groups.count(group_id)) {
            printf("[sync] LEAVE_GROUP for unknown group '%s', ignoring\n", group_id.c_str());
            return;
        }
        raw_leave_group(group_id, username);
        printf("[sync] applied LEAVE_GROUP %s %s\n", group_id.c_str(), username.c_str());
        return;
    }

    printf("[sync] unknown sync op: %s\n", opname.c_str());
}

void sync_thread_func(int tracker_no, TrackerAddr self, TrackerAddr peer) {
    int sync_listen_fd = -1;

    if (tracker_no == 1) {
        sync_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(sync_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(self.port + 100);

        bind(sync_listen_fd, (sockaddr*)&addr, sizeof(addr));
        listen(sync_listen_fd, 1);
    }

    while (true) {
        int sync_fd = -1;

        if (tracker_no == 1) {
            printf("[sync] Waiting for peer tracker to connect on port %d...\n", self.port + 100);
            sockaddr_in peer_addr{};
            socklen_t peer_len = sizeof(peer_addr);
            sync_fd = accept(sync_listen_fd, (sockaddr*)&peer_addr, &peer_len);
            printf("[sync] Peer tracker connected.\n");

        } else {
            while (true) {
                sync_fd = socket(AF_INET, SOCK_STREAM, 0);
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(peer.port + 100);
                inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr);
                if (connect(sync_fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
                    printf("[sync] Connected to peer tracker.\n");
                    break;
                }
                close(sync_fd);
                printf("[sync] Peer tracker not up yet, retrying...\n");
                this_thread::sleep_for(chrono::seconds(1));
            }
        }

        g_sync_fd = sync_fd;

        if (tracker_no == 1) {
            string msg;
            recv_message(sync_fd, msg);
            printf("[sync] Received: %s\n", msg.c_str());
            send_message(sync_fd, "SYNC_ACK");
        } else {
            send_message(sync_fd, "SYNC_HELLO");
            string reply;
            recv_message(sync_fd, reply);
            printf("[sync] Received: %s\n", reply.c_str());
        }

        {
            vector<string> log_snapshot;
            {
                lock_guard<mutex> lock(g_state_mutex);
                log_snapshot = g_op_log;
            }
            printf("[sync] sending catch-up: %zu ops\n", log_snapshot.size());
            for (const string& op_line : log_snapshot) {
                sync_send(op_line);
            }
        }

        while (true) {
            string msg;
            if (!recv_message(sync_fd, msg)) {
                printf("[sync] Peer tracker disconnected.\n");
                break;
            }
            printf("[sync] Received: %s\n", msg.c_str());
            if (msg.rfind("SYNC_OP", 0) == 0) {
                apply_sync_op(msg);
            }
        }

        close(sync_fd);
        g_sync_fd = -1;
        printf("[sync] Attempting to re-establish tracker connection...\n");
    }
}

vector<TrackerAddr> read_tracker_info(const string& path) {
    ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "Could not open %s\n", path.c_str());
        exit(1);
    }

    vector<TrackerAddr> addrs;
    string ip;
    int port;
    while (file >> ip >> port) {
        addrs.push_back({ip, port});
    }

    if (addrs.size() < 2) {
        fprintf(stderr, "tracker_info.txt must have at least 2 tracker entries\n");
        exit(1);
    }
    return addrs;
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    if (argc < 3) {
        fprintf(stderr, "Usage: %s tracker_info.txt tracker_no\n", argv[0]);
        return 1;
    }
    string info_path = argv[1];
    int tracker_no = atoi(argv[2]);   // 1 or 2
    g_tracker_no = tracker_no;
    g_start_epoch = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    vector<TrackerAddr> addrs = read_tracker_info(info_path);
    if (tracker_no < 1 || tracker_no > (int)addrs.size()) {
        fprintf(stderr, "tracker_no must be between 1 and %zu\n", addrs.size());
        return 1;
    }
    TrackerAddr self = addrs[tracker_no - 1];
    TrackerAddr peer = addrs[tracker_no == 1 ? 1 : 0];
    thread sync_thread(sync_thread_func, tracker_no, self, peer);
    sync_thread.detach();
    int port = self.port;

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
    printf("Tracker %d listening on %s:%d...\n", tracker_no, self.ip.c_str(), port);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }
        thread client_thread(handle_client, client_fd);
        client_thread.detach();
    }
    close(listen_fd);
    return 0;
}