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
    int candidateId;
    int leaderId;
};

class Node {
public:
    Node(std::vector<std::string> peers);
    void run();

private:
    void follower_loop();
    void candidate_loop();
    void leader_loop();
    void send_msg(in_addr_t ip, const Message& msg);
    void listen();
    void handle_msg(const Message& msg, in_addr_t fromIp);
    void reset_timeout();
    static in_addr_t get_ip();

    in_addr_t ip;
    std::vector<in_addr_t> peerIps;
    std::atomic<Role> role;
    std::atomic<int> currentTerm;
    std::atomic<int> votedFor;
    std::mutex mtx;
    std::condition_variable cv;
    int serverSock;
    std::thread listenerThread;
    int votesReceived;
    std::chrono::steady_clock::time_point electionDeadline;
};

}