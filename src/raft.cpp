#include "raft.h"
#include <netdb.h>
#include <unistd.h>
#include <random>
#include <thread>
#include <spdlog/spdlog.h>
#include <magic_enum/magic_enum.hpp>

using namespace raft;

Node::Node(std::initializer_list<std::string> peerIps)
    : ip{get_ip()}, role{Follower}, commitIndex{0} {
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
    RPC msg;
    while (true) {
        sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        if (recvfrom(server_sock, &msg, sizeof(msg), MSG_PEEK, reinterpret_cast<sockaddr*>(&from), &fromlen) == 0) continue;
        if (msg.term > currentTerm) {
            currentTerm = msg.term;
            role = Follower;
            votedFor = std::nullopt;
            reset_timeout();
        }

        if (msg.type == RequestVote) {
            RequestVoteRPC msg;
            if (recv(server_sock, &msg, sizeof(msg), 0) > 0) {
                if (role == Follower && !votedFor.load().has_value()) {
                    VoteReceivedRPC response;
                    if (msg.term < currentTerm)
                        response.voteGranted = false;
                    else {
                        response.voteGranted = true; 
                        votedFor = from.sin_addr.s_addr;
                        reset_timeout();
                    }
                    send_msg(from.sin_addr.s_addr, &response, sizeof(response));
                }
            }
        } else if (msg.type == VoteReceived) {
            VoteReceivedRPC msg;
            if (recv(server_sock, &msg, sizeof(msg), 0) > 0 && msg.voteGranted)
                votes_received++;
        } else if (msg.type == AppendEntries) {
            std::unique_ptr<AppendEntriesRPC> msg = std::make_unique<AppendEntriesRPC>();
            if (!recv(server_sock, msg.get(), sizeof(AppendEntriesRPC), MSG_PEEK)) break;
            size_t size = sizeof(AppendEntriesRPC) + msg->numEntries * sizeof(LogEntry);
            msg = std::unique_ptr<AppendEntriesRPC>{static_cast<AppendEntriesRPC*>(operator new(size))};
            if (recv(server_sock, msg.get(), size, 0) > 0) {
                reset_timeout();
                AppendEntriesReceivedRPC response;
                response.success = msg->term >= currentTerm && (!msg->prevLogIndex || (msg->prevLogIndex <= log.size() && msg->prevLogTerm == log[msg->prevLogIndex.value()].term));
                if (response.success) {
                    commitIndex = msg->leaderCommit;
                    if (msg->prevLogIndex)
                        commitIndex = std::min(commitIndex.load(), msg->prevLogIndex.value() + msg->numEntries);
                    for (int i = 0; i < msg->numEntries; i++)
                        log.push_back(msg->entries[i]);
                }
                response.nextIndex = log.size();
                send_msg(from.sin_addr.s_addr, &response, sizeof(response));
            }
        } else if (msg.type == AppendEntriesReceived) {
            AppendEntriesReceivedRPC msg;
            if (recv(server_sock, &msg, sizeof(msg), 0) > 0) {
                nextIndex[from.sin_addr.s_addr] = msg.nextIndex;
                matchIndex[from.sin_addr.s_addr] = msg.nextIndex;
                for (size_t i = log.size(); i > commitIndex; i--) {
                    size_t count = 1;
                    for (auto mi : matchIndex)
                        if (mi.second >= i) count++;
                    if (count > peers.size() / 2 && log[i - 1].term == currentTerm) {
                        commitIndex = i;
                        break;
                    }
                }
                if (msg.success) {
                    spdlog::info("Node successfully applied AppendEntries, commit index is {}", commitIndex.load());
                } else {
                    spdlog::warn("Node failed to apply AppendEntries");
                }
            }
        }
        cv.notify_all();
    }
}

void Node::send_msg(in_addr_t ip, RPC *msg, size_t length) {
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

    RequestVoteRPC msg;
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
    for (in_addr_t peer : peers) {
        nextIndex[peer] = log.size();
        matchIndex[peer] = 0;
    }
    while (role == Leader) {
        for (in_addr_t peer : peers) {
            if (peer == ip) continue;
            size_t numEntries = log.size() - nextIndex[peer];
            size_t size = sizeof(AppendEntriesRPC) + sizeof(LogEntry) * numEntries;
            std::unique_ptr<AppendEntriesRPC> msg{static_cast<AppendEntriesRPC*>(operator new(size))};
            *msg = {};
            msg->numEntries = numEntries;
            for (size_t i = 0; i < numEntries; i++)
                msg->entries[i] = log[nextIndex[peer] + i];
            msg->prevLogIndex = std::nullopt;
            if (nextIndex[peer]) {
                msg->prevLogIndex = nextIndex[peer] - 1;
                msg->prevLogTerm = log[msg->prevLogIndex.value()].term;
            }
            msg->leaderCommit = commitIndex;
            send_msg(peer, msg.get(), size);
        }
        {
            std::unique_lock<std::mutex> lock{mtx};
            if (!cv.wait_for(lock, std::chrono::milliseconds{100}, [this] { return commitIndex == log.size(); }))
                spdlog::warn("Leader timed out while waiting for commit confirmation");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
}

void Node::reset_timeout() {
    std::mt19937 rng{std::random_device{}()};
    election_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{std::uniform_int_distribution<size_t>{500, 1000}(rng)};
}