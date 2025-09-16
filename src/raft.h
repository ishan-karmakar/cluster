#pragma once
#include <atomic>
#include <thread>
#include <condition_variable>
#include <optional>
#include <netinet/in.h>
#include <vector>
#include <string>

namespace raft {

// Raft roles
enum Role { Follower, Candidate, Leader };

enum MessageType {
    RequestVote,
    AppendEntries,
    Received,
    AppendEntriesReceived
};

struct Message {
    MessageType type;
    int term;
};

struct RequestVoteMessage : Message {
    RequestVoteMessage() : Message{RequestVote} {}
};

struct LogEntry {
    int term;
    std::string command;
};

struct AppendEntriesMessage : Message {
    AppendEntriesMessage() : Message{AppendEntries} {}
    int prevLogIndex;
    int prevLogTerm;
    int leaderCommit;
    size_t entryCount;
    // For simplicity, fixed-size array; in production, use serialization
    LogEntry entries[10];
};

struct ReceivedMessage : Message {
    ReceivedMessage() : Message{Received} {}
};

struct AppendEntriesResponseMessage : Message {
    AppendEntriesResponseMessage() : Message{AppendEntriesReceived} {}
    bool success;
    int matchIndex;
};

class Node {
public:
    Node(std::vector<std::string> peers);
    void run();

private:
    void follower_loop();
    void candidate_loop();
    void leader_loop();

    void send_msg(in_addr_t ip, Message *msg, size_t length);

    void listen();

    void reset_timeout();
    static in_addr_t get_ip();
    bool is_majority();

    void handle_request_vote();
    void handle_received();
    void handle_append_entries();

    in_addr_t ip;
    std::vector<in_addr_t> peerIps;
    std::atomic<Role> role;
    std::atomic<int> currentTerm;
    std::atomic<std::optional<in_addr_t>> votedFor;
    std::mutex mtx;
    std::condition_variable cv;
    int serverSock;
    std::thread listenerThread;
    int nodesReceived;
    std::chrono::steady_clock::time_point electionDeadline;

    // Log replication state
    std::vector<LogEntry> log;
    int commitIndex = 0;
    int lastApplied = 0;
    std::vector<int> nextIndex;
    std::vector<int> matchIndex;
};

}