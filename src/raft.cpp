#include "raft.h"
#include <netdb.h>
#include <unistd.h>
#include <random>
#include <magic_enum/magic_enum.hpp>
#include <spdlog/spdlog.h>

using namespace raft;

Node::Node(std::vector<std::string> peers)
    : ip{get_ip()}, role(Role::Follower), currentTerm(0), votedFor(-1), serverSock(-1) {
    // Resolve peer strings to in_addr_t
    for (const auto& peer : peers) {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(peer.c_str(), nullptr, &hints, &res) == 0 && res) {
            in_addr_t addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
            peerIps.push_back(addr);
            freeaddrinfo(res);
        }
    }
    reset_timeout();
}

in_addr_t Node::get_ip() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0 && res) {
            in_addr_t addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
            freeaddrinfo(res);
            return addr;
        }
    }
    std::exit(1);
}

void Node::run() {
    listenerThread = std::thread(&Node::listen, this);
    while (true) {
        switch (role.load()) {
            case Role::Follower:
                follower_loop();
                break;
            case Role::Candidate:
                candidate_loop();
                break;
            case Role::Leader:
                leader_loop();
                break;
        }
    }
    if (listenerThread.joinable()) listenerThread.join();
}

void Node::follower_loop() {
    spdlog::info("This node is now a follower");
    std::unique_lock<std::mutex> lock(mtx);
    while (role == Role::Follower) {
        if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout)
            role = Role::Candidate;
    }
}

void Node::candidate_loop() {
    spdlog::info("This node is now a candidate");
    {
        std::lock_guard<std::mutex> lock(mtx);
        currentTerm++;
        nodesReceived = 1; // Vote for self
    }
    // Send RequestVote to all peers (except self)
    RequestVoteMessage msg;
    for (auto peerIp : peerIps) {
        // Replace with check for current IP
        if (peerIp != ip) send_msg(peerIp, &msg, sizeof(msg));
    }
    reset_timeout();
    std::unique_lock<std::mutex> lock(mtx);
    while (role == Role::Candidate) {
        if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout) {
            // Election failed, start new election
            break;
        }
        // If votes > half, become leader
        if (is_majority()) {
            role = Role::Leader;
            break;
        }
    }
}

void Node::leader_loop() {
    spdlog::info("This node is now the leader");
    while (role == Role::Leader) {
        auto msg = (AppendEntriesMessage*)::operator new(sizeof(AppendEntriesMessage) + 5);
        *msg = AppendEntriesMessage{};
        for (const auto& ip : peerIps) send_msg(ip, msg, sizeof(AppendEntriesMessage) + 5);
        nodesReceived = 1; // Leader counts itself

        // Wait for majority of ReceivedMessage responses or timeout
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return is_majority(); });

        // Proceed to next round after waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}

bool Node::is_majority() {
    return nodesReceived > peerIps.size() / 2;
}

void Node::send_msg(in_addr_t ip, Message *msg, size_t length) {
    msg->term = currentTerm;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dest = { 0 };
    dest.sin_port = htons(5000);
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = ip;
    sendto(sock, msg, length, 0, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    close(sock);
}

void Node::listen() {
    serverSock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(serverSock, (sockaddr*)&addr, sizeof(addr));
    Message msg;
    while (true) {
        Message msg;
        if (recv(serverSock, &msg, sizeof(msg), MSG_PEEK) == 0) continue;
        std::lock_guard<std::mutex> lock{mtx};
        if (msg.term > currentTerm) {
            currentTerm = msg.term;
            role = Follower;
            votedFor = std::nullopt;
            reset_timeout();
        }
        if (msg.type == RequestVote) handle_request_vote();
        else if (msg.type == Received) handle_received();
        else if (msg.type == AppendEntries) handle_append_entries();
        cv.notify_all();
    }
    close(serverSock);
}

void Node::handle_request_vote() {
    sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    RequestVoteMessage msg;
    if (recvfrom(serverSock, &msg, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&from), &fromlen) == 0) return;
    if ((!votedFor.load().has_value() || votedFor.load() == from.sin_addr.s_addr) && msg.term >= currentTerm) {
        votedFor = from.sin_addr.s_addr;
        reset_timeout();
        ReceivedMessage msg;
        send_msg(from.sin_addr.s_addr, &msg, sizeof(msg));
    }
}

void Node::handle_received() {
    nodesReceived++;
    cv.notify_all();
}

void Node::handle_append_entries() {
    reset_timeout();
    AppendEntriesMessage msg;
    size_t length = sizeof(msg);
    if (recv(serverSock, &msg, length, MSG_PEEK) == 0) return;
    length += msg.length;
    sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    if (recvfrom(serverSock, &msg, length, 0, reinterpret_cast<sockaddr*>(&from), &fromlen) == 0) return;
    ReceivedMessage received_msg;
    send_msg(from.sin_addr.s_addr, &received_msg, sizeof(received_msg));
}

void Node::reset_timeout() {
    static std::mt19937 rng;
    electionDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::uniform_int_distribution<size_t>{300, 500}(rng));
}
