#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <random>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <sstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace raft {

// Raft roles
enum Role { Follower, Candidate, Leader };

enum MessageType {
    RequestVote,
    AppendEntries,
    VoteResponse
};

struct Message {
    MessageType type;
    int term;
};

struct RequestVoteMessage : Message {
    RequestVoteMessage() : Message{RequestVote} {}
};

struct AppendEntriesMessage : Message {
    AppendEntriesMessage() : Message{AppendEntries} {}
};

struct VoteResponseMessage : Message {
    VoteResponseMessage() : Message{VoteResponse} {}
};

class Node {
public:
    Node(std::vector<std::string> peers);
    void run();

private:
    void follower_loop();
    void candidate_loop();
    void leader_loop();

    template <typename T>
    void send_msg(in_addr_t ip, T& msg);

    void listen();

    void reset_timeout();
    static in_addr_t get_ip();

    void handle_request_vote();
    void handle_vote_response();

    in_addr_t ip;
    std::vector<in_addr_t> peerIps;
    std::atomic<Role> role;
    std::atomic<int> currentTerm;
    std::atomic<in_addr_t> votedFor;
    std::mutex mtx;
    std::condition_variable cv;
    int serverSock;
    std::thread listenerThread;
    int votesReceived;
    std::chrono::steady_clock::time_point electionDeadline;
};

}