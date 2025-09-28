#pragma once
#include <initializer_list>
#include <string>
#include <atomic>
#include <condition_variable>
#include <netinet/in.h>
#include <optional>
#include <vector>
#include <unordered_map>

namespace raft {

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

struct LogEntry {
    size_t term;
    int value;
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

struct AppendEntriesRPC : RPC {
    AppendEntriesRPC() : RPC{AppendEntries} {}

    std::optional<size_t> prevLogIndex;
    size_t prevLogTerm;
    size_t leaderCommit;
    bool containsEntry;
    LogEntry entry;
};

struct AppendEntriesReceivedRPC : RPC {
    AppendEntriesReceivedRPC() : RPC{AppendEntriesReceived} {}

    bool success;
    size_t nextIndex;
};

class Node {
public:
    Node(std::initializer_list<std::string> peers);
    void operator()();

private:
    void follower_loop();
    void candidate_loop();
    void leader_loop();

    void send_msg(in_addr_t ip, RPC*, size_t);

    void listen();

    void reset_timeout();

    static in_addr_t get_ip();

    in_addr_t ip;
    std::vector<in_addr_t> peers;
    std::atomic<Role> role;
    int server_sock;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<size_t> votes_received;
    std::chrono::steady_clock::time_point election_deadline;
    
    // Raft states
    std::atomic<size_t> currentTerm;
    std::atomic<std::optional<in_addr_t>> votedFor;
    std::vector<LogEntry> log;
    std::atomic<size_t> commitIndex;
    std::atomic<size_t> lastApplied;
    std::unordered_map<in_addr_t, size_t> nextIndex;
    std::unordered_map<in_addr_t, size_t> matchIndex;
};

}