#pragma once
#include <initializer_list>
#include <string>
#include <atomic>
#include <condition_variable>
#include <netinet/in.h>
#include <optional>
#include <vector>

namespace raft {

enum MessageType {
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

struct Message {
    MessageType type;
    int term;
};

struct LogEntry {
    int term;
    std::string command;
};

struct RequestVoteMessage : Message {
    RequestVoteMessage() : Message{RequestVote} {}
};

struct VoteReceivedMessage : Message {
    VoteReceivedMessage() : Message{VoteReceived} {}
};

struct AppendEntriesMessage : Message {
    AppendEntriesMessage() : Message{AppendEntries} {}

    int prev_log_index;
    int prev_log_term;
    int leader_commit;
    size_t entry_count;
    LogEntry entries[10];
};

struct AppendEntriesReceivedMessage : Message {
    AppendEntriesReceivedMessage() : Message{AppendEntriesReceived} {}

    bool success;
};

class Node {
public:
    Node(std::initializer_list<std::string> peers);
    void operator()();

private:
    void follower_loop();
    void candidate_loop();
    void leader_loop();

    void send_msg(in_addr_t ip, Message*, size_t);

    void listen();

    void reset_timeout();

    static in_addr_t get_ip();

    in_addr_t ip;
    std::vector<in_addr_t> peers;
    std::atomic<Role> role;
    std::atomic<size_t> cur_term;
    std::atomic<std::optional<in_addr_t>> voted_for;
    int server_sock;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<size_t> votes_received;
    std::atomic<size_t> commits_received;
    std::chrono::steady_clock::time_point election_deadline;

    // Log replication state
    std::vector<LogEntry> log;
    int commit_idx;
    int lastApplied = 0;
    std::vector<int> nextIndex;
    std::vector<int> matchIndex;
};

}