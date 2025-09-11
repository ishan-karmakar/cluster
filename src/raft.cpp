#include "raft.h"
#include <spdlog/spdlog.h>
#include <netdb.h>
#include <magic_enum/magic_enum.hpp>

using namespace raft;

Node::Node(std::vector<std::string> peers)
    : ip{get_ip()}, role(Role::Follower), currentTerm(0), votedFor(-1), serverSock(-1) {
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
    spdlog::critical("Error getting node IP address");
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
    spdlog::info("Node became follower");
    std::unique_lock<std::mutex> lock(mtx);
    while (role == Role::Follower) {
        if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout) {
            spdlog::trace("Election deadline exceeded");
            role = Role::Candidate;
        }
    }
}

void Node::candidate_loop() {
    spdlog::info("Node became candidate");
    {
        std::lock_guard<std::mutex> lock(mtx);
        currentTerm++;
        votesReceived = 1; // Vote for self
    }
    // Send RequestVote to all peers (except self)
    Message msg{RequestVote, currentTerm, -1};
    for (auto peerIp : peerIps) {
        // Replace with check for current IP
        if (peerIp != ip) send_msg(peerIp, msg);
    }
    reset_timeout();
    std::unique_lock<std::mutex> lock(mtx);
    while (role == Role::Candidate) {
        if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout) {
            // Election failed, start new election
            break;
        }
        // If votes > half, become leader
        if (votesReceived > (int)peerIps.size() / 2) {
            role = Role::Leader;
            break;
        }
    }
}

void Node::leader_loop() {
    spdlog::info("Node became leader for term {}", currentTerm.load());
    while (role == Role::Leader) {
        Message msg{AppendEntries, currentTerm, -1};
        for (const auto& ip : peerIps) send_msg(ip, msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}

void Node::send_msg(in_addr_t ip, const Message& msg) {
    // spdlog::debug("Sending {} to {}", magic_enum::enum_name(msg.type), hostname);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dest = { 0 };
    dest.sin_port = htons(5000);
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = ip;
    sendto(sock, &msg, sizeof(msg), 0, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    close(sock);
}

void Node::listen() {
    serverSock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000); //
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(serverSock, (sockaddr*)&addr, sizeof(addr));
    Message msg;
    while (true) {
        sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        if (recvfrom(serverSock, &msg, sizeof(msg), 0, (sockaddr*)&from, &fromlen) > 0)
            handle_msg(msg, from.sin_addr.s_addr);
    }
    close(serverSock);
}

void Node::handle_msg(const Message& msg, in_addr_t fromIp) {
    std::lock_guard<std::mutex> lock(mtx);
    if (msg.term > currentTerm) {
        currentTerm = msg.term;
        role = Role::Follower;
        votedFor = -1;
        reset_timeout();
    }
    // spdlog::debug("Received {} from {}", magic_enum::enum_name(msg.type), fromIp);
    if (msg.type == RequestVote) {
        if ((votedFor == -1 || votedFor == msg.candidateId) && msg.term >= currentTerm) {
            votedFor = msg.candidateId;
            reset_timeout();
            // Send vote
            Message voteMsg{VoteResponse, currentTerm, -1, -1};
            send_msg(fromIp, voteMsg);
        }
    } else if (msg.type == VoteResponse) {
        if (role == Role::Candidate && msg.term == currentTerm) {
            votesReceived++;
            cv.notify_all();
        }
    } else if (msg.type == AppendEntries) {
        reset_timeout();
    }
    cv.notify_all();
}

void Node::reset_timeout() {
    spdlog::trace("Resetting election timeout");
    static std::mt19937 rng(std::random_device{}());
    electionDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::uniform_int_distribution<size_t>{300, 500}(rng));
}
