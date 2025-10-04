#pragma once
#include <initializer_list>
#include <string>
#include <atomic>
#include <condition_variable>
#include <netinet/in.h>
#include <optional>
#include <vector>
#include <unordered_map>
#include <thread>
#include <random>
#include <iostream>
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <netdb.h>

namespace safehouse {
namespace cluster {

enum RPCType {
    RequestVote,
    AppendEntries,
    VoteReceived,
    AppendEntriesReceived
};

enum Role {
    Follower,
    Candidate,
    Leader
};

struct RPC {
    RPCType type;
    size_t term;
};

template <typename T>
struct LogEntry {
    size_t term;
    T value;
};

struct RequestVoteRPC : RPC {
    RequestVoteRPC() : RPC{RequestVote} {}

    in_addr_t candidateId;
    size_t lastLogIndex;
    size_t lastLogTerm;
};

struct VoteReceivedRPC : RPC {
    VoteReceivedRPC() : RPC{VoteReceived} {}

    bool voteGranted;
};

template <typename T>
struct AppendEntriesRPC : RPC {
    AppendEntriesRPC() : RPC{AppendEntries} {}

    std::optional<size_t> prevLogIndex;
    size_t prevLogTerm;
    size_t leaderCommit;
    size_t numEntries;
    LogEntry<T> entries[0];
};

struct AppendEntriesReceivedRPC : RPC {
    AppendEntriesReceivedRPC() : RPC{AppendEntriesReceived} {}

    bool success;
    size_t nextIndex;
};

template <typename T>
class Node {
public:
    Node(std::initializer_list<std::string> peerIps) : ip{get_ip()}, role{Follower}, commitIndex{0} {
        for (const auto& peer : peerIps) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            if (getaddrinfo(peer.c_str(), nullptr, &hints, &res) == 0 && res) {
                in_addr_t addr = ((struct sockaddr_in*) res->ai_addr)->sin_addr.s_addr;
                peers.push_back(addr);
                freeaddrinfo(res);
            }
        }
        reset_timeout();
    }

    void operator()() {
        std::thread listener{&Node::listen, this};
        std::thread main_thread{[this] {
            while (true) {
                if (role == Follower) follower_loop();
                else if (role == Candidate) candidate_loop();
                else leader_loop();
            }
        }};
        main_thread.join();
        listener.join();
    }

private:
    void follower_loop() {
        spdlog::info("This node is now a follower");
        while (role == Follower) {
            std::unique_lock<std::mutex> lock{cvMutex};
            if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout)
                role = Candidate;
        }
    }

    void candidate_loop() {
        spdlog::info("This node is now a candidate");
        currentTerm++;
        votesReceived = 1;
        votedFor = std::nullopt;

        RequestVoteRPC msg;
        for (auto peer : peers)
            if (peer != ip) send_msg(peer, &msg, sizeof(msg));
        
        reset_timeout();
        while (role == Candidate) {
            {
                std::unique_lock<std::mutex> lock{cvMutex};
                if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout) {
                    spdlog::warn("Election failed, time to start a new election");
                    break;
                }
            }
            if (votesReceived > peers.size() / 2) {
                role = Leader;
                break;
            }
        }
    }

    void leader_loop() {
        spdlog::info("This node is now the leader");
        for (auto peer : peers) {
            std::lock_guard<std::mutex> lock{stateMutex};
            nextIndex[peer] = log.size();
            matchIndex[peer] = 0;
        }
        while (role == Leader) {
            for (auto peer : peers) {
                if (peer == ip) continue;
                std::lock_guard<std::mutex> lock{stateMutex};
                size_t numEntries = log.size() - nextIndex[peer];
                size_t size = sizeof(AppendEntriesRPC<T>) + sizeof(LogEntry<T>) * numEntries;
                std::unique_ptr<AppendEntriesRPC<T>> msg{static_cast<AppendEntriesRPC<T>*>(operator new(size))};
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
                std::unique_lock<std::mutex> lock{cvMutex};
                if (!cv.wait_for(lock, std::chrono::milliseconds{100}, [this] {
                    std::lock_guard<std::mutex> lock{stateMutex};
                    return commitIndex == log.size();
                })) {
                    spdlog::warn("Leader timed out while waiting for commit confirmation");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
        }
    }

    void send_msg(in_addr_t ip, RPC *msg, size_t length) {
        msg->term = currentTerm;
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in dest = { 0 };
        dest.sin_port = htons(5000);
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = ip;
        sendto(sock, msg, length, 0, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
        close(sock);
    }

    void listen() {
        serverSock = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5000);
        addr.sin_addr.s_addr=  INADDR_ANY;
        bind(serverSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        RPC msg;
        while (true) {
            sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            if (recvfrom(serverSock, &msg, sizeof(msg), MSG_PEEK, reinterpret_cast<sockaddr*>(&from), &fromlen) == 0) continue;
            if (msg.term > currentTerm) {
                currentTerm = msg.term;
                role = Follower;
                votedFor = std::nullopt;
                reset_timeout();
            }

            if (msg.type == RequestVote) {
                RequestVoteRPC msg;
                if (recv(serverSock, &msg, sizeof(msg), 0) > 0) {
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
                if (recv(serverSock, &msg, sizeof(msg), 0) > 0 && msg.voteGranted)
                    votesReceived++;
            } else if (msg.type == AppendEntries) {
                auto msg = std::make_unique<AppendEntriesRPC<T>>();
                if (!recv(serverSock, msg.get(), sizeof(AppendEntriesRPC<T>), MSG_PEEK)) break;
                size_t size = sizeof(AppendEntriesRPC<T>) + msg->numEntries * sizeof(LogEntry<T>);
                msg = std::unique_ptr<AppendEntriesRPC<T>>{static_cast<AppendEntriesRPC<T>*>(operator new(size))};
                if (recv(serverSock, msg.get(), size, 0) > 0) {
                    reset_timeout();
                    AppendEntriesReceivedRPC response;
                    response.success = msg->term >= currentTerm && (!msg->prevLogIndex || (msg->prevLogIndex <= log.size() && msg->prevLogTerm == log[msg->prevLogIndex.value()].term));
                    std::lock_guard<std::mutex> lock{stateMutex};
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
                if (recv(serverSock, &msg, sizeof(msg), 0) > 0) {
                    {
                        std::lock_guard<std::mutex> lock{stateMutex};
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
                    }
                    if (!msg.success)
                        spdlog::warn("Node failed to apply AppendEntries");
                    else spdlog::info("Node successfully applied AppendEntries");
                }
            }
            cv.notify_all();
        }
    }

    void reset_timeout() {
        std::mt19937 rng{std::random_device{}()};
        electionDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{std::uniform_int_distribution<size_t>{500, 1000}(rng)};
    }

    static in_addr_t get_ip() {
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

    in_addr_t ip;
    std::vector<in_addr_t> peers;
    std::atomic<Role> role;
    int serverSock;
    std::mutex cvMutex, stateMutex;
    std::condition_variable cv;
    std::atomic<size_t> votesReceived;
    std::chrono::steady_clock::time_point electionDeadline;
    
    // Raft states
    std::atomic<size_t> currentTerm;
    std::atomic<std::optional<in_addr_t>> votedFor;
    std::vector<LogEntry<T>> log;
    std::atomic<size_t> commitIndex;
    std::atomic<size_t> lastApplied;
    std::unordered_map<in_addr_t, size_t> nextIndex;
    std::unordered_map<in_addr_t, size_t> matchIndex;
};

}
}