#pragma once
#include <atomic>
#include <thread>
#include <condition_variable>
#include <optional>
#include <netinet/in.h>
#include <vector>

namespace raft {

// Raft roles
enum Role { Follower, Candidate, Leader };

enum MessageType {
    RequestVote,
    AppendEntries,
    Received
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

    size_t length;
    char data[0];
};

struct ReceivedMessage : Message {
    ReceivedMessage() : Message{Received} {}
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
};

}