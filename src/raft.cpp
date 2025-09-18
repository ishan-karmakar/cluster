#include "raft.h"
#include <netdb.h>
#include <unistd.h>
#include <random>
#include <thread>
#include <spdlog/spdlog.h>
#include <magic_enum/magic_enum.hpp>

using namespace raft;

Node::Node(std::initializer_list<std::string> peerIps)
    : ip{get_ip()}, role{Follower} {
    for (const auto& peer : peerIps) {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(peer.c_str(), nullptr, &hints, &res) == 0 && res) {
            in_addr_t addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
            peers.push_back(addr);
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
    spdlog::critical("Could not find own IP address, aborting...");
    std::exit(1);
}

void Node::operator()() {
    std::thread listener = std::thread(&Node::listen, this);
    while (true) {
        switch (role.load()) {
            case Follower:
                follower_loop();
                break;
            case Candidate:
                candidate_loop();
                break;
            case Leader:
                leader_loop();
                break;
        }
    }
    listener.join();
}

void Node::listen() {
    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000);
    addr.sin_addr.s_addr=  INADDR_ANY;
    bind(server_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    Message msg;
    while (true) {
        sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        Message msg;
        if (recvfrom(server_sock, &msg, sizeof(msg), MSG_PEEK, reinterpret_cast<sockaddr*>(&from), &fromlen) == 0) continue;
        if (msg.term > currentTerm) {
            currentTerm = msg.term;
            role = Follower;
            votedFor = std::nullopt;
            reset_timeout();
        }

        if (msg.type == RequestVote) {
            RequestVoteMessage msg;
            if (recv(server_sock, &msg, sizeof(msg), 0) > 0) {
                if (role == Follower && !votedFor.load().has_value()) {
                    spdlog::info("Sending a vote");
                    votedFor = from.sin_addr.s_addr;
                    reset_timeout();
                    VoteReceivedMessage msg;
                    send_msg(votedFor.load().value(), &msg, sizeof(msg));
                }
            }
        } else if (msg.type == VoteReceived) {
            spdlog::info("Received a vote");
            VoteReceivedMessage msg;
            if (recv(server_sock, &msg, sizeof(msg), 0) > 0)
                votes_received++;
        } else if (msg.type == AppendEntries) {
            AppendEntriesMessage msg;
            if (recv(server_sock, &msg, sizeof(msg), 0) > 0) {
                reset_timeout();
                AppendEntriesReceivedMessage response;
                if (msg.term < currentTerm) {
                    response.success = false;
                    send_msg(from.sin_addr.s_addr, &msg, sizeof(msg));
                } else if (msg.prevLogIndex != 0) {
                    if (!(msg.prevLogIndex <= log.size() && msg.prevLogTerm != log[msg.prevLogIndex - 1].term)) {
                        response.success = false;
                        send_msg(from.sin_addr.s_addr, &msg, sizeof(msg));
                    }
                }
            }
        } else if (msg.type == AppendEntriesReceived)
            AppendEntriesReceivedMessage msg;
            if (recv(server_sock, &msg, sizeof(msg), 0) > 0)
                commits_received++;
        cv.notify_all();
    }
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

void Node::follower_loop() {
    spdlog::info("This node is now a follower");
    while (role == Follower) {
        std::unique_lock<std::mutex> lock{mtx};
        if (cv.wait_until(lock, election_deadline) == std::cv_status::timeout)
            role = Candidate;
    }
}

void Node::candidate_loop() {
    spdlog::info("This node is now a candidate");
    currentTerm++;
    votes_received = 1;
    votedFor = std::nullopt;

    RequestVoteMessage msg;
    for (auto peer : peers)
        if (peer != ip) send_msg(peer, &msg, sizeof(msg));

    reset_timeout();
    while (role == Candidate) {
        {
            std::unique_lock<std::mutex> lock{mtx};
            if (cv.wait_until(lock, election_deadline) == std::cv_status::timeout) {
                spdlog::debug("Election failed, time to start a new election");
                break;
            }
        }
        if (votes_received > peers.size() / 2) {
            role = Leader;
            break;
        }
    }
}

void Node::leader_loop() {
    spdlog::info("This node is now the leader");
    {
        std::lock_guard<std::mutex> lock(mtx);
        nextIndex.assign(peers.size(), log.size());
        matchIndex.assign(peers.size(), 0);
    }
    while (role == Leader) {
        commits_received = 1;
        for (size_t i = 0; i < peers.size(); i++) {
            if (peers[i] == ip) continue;
            AppendEntriesMessage msg;
            msg.prevLogIndex = nextIndex[i] - 1;
            send_msg(peers[i], &msg, sizeof(msg));
        }
        {
            std::unique_lock<std::mutex> lock{mtx};
            cv.wait_for(lock, std::chrono::milliseconds{150}, [this] { return commits_received > peers.size() / 2; });
        }
        spdlog::info("AppendEntries was committed");
    }
}

void Node::reset_timeout() {
    std::mt19937 rng{std::random_device{}()};
    election_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{std::uniform_int_distribution<size_t>{500, 1000}(rng)};
}