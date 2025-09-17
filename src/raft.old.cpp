#include "raft.h"
#include <netdb.h>
#include <unistd.h>
#include <random>
#include <magic_enum/magic_enum.hpp>
#include <spdlog/spdlog.h>

using namespace raft;

Node::Node(std::vector<std::string> peers)
    : ip{get_ip()}, role(Role::Follower), currentTerm(0), votedFor(-1), serverSock(-1) {
    // Resolve peer strings to in_addr_t
    for (const auto& peer : peers) {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(peer.c_str(), nullptr, &hints, &res) == 0 && res) {
            in_addr_t addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
            peerIps.push_back(addr);
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
    spdlog::info("This node is now a follower");
    std::unique_lock<std::mutex> lock(mtx);
    while (role == Role::Follower) {
        if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout)
            role = Role::Candidate;
    }
}

void Node::candidate_loop() {
    spdlog::info("This node is now a candidate");
    {
        std::lock_guard<std::mutex> lock(mtx);
        currentTerm++;
        nodesReceived = 1; // Vote for self
    }
    // Send RequestVote to all peers (except self)
    RequestVoteMessage msg;
    for (auto peerIp : peerIps) {
        // Replace with check for current IP
        if (peerIp != ip) send_msg(peerIp, &msg, sizeof(msg));
    }
    reset_timeout();
    std::unique_lock<std::mutex> lock(mtx);
    while (role == Role::Candidate) {
        if (cv.wait_until(lock, electionDeadline) == std::cv_status::timeout) {
            // Election failed, start new election
            break;
        }
        // If votes > half, become leader
        if (is_majority()) {
            role = Role::Leader;
            break;
        }
    }
}

void Node::leader_loop() {
    spdlog::info("This node is now the leader");
    {
        std::lock_guard<std::mutex> lock(mtx);
        nextIndex.assign(peerIps.size(), log.size());
        matchIndex.assign(peerIps.size(), 0);
    }
    while (role == Role::Leader) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            nodesReceived = 1; // Leader counts itself
        }
        // Send AppendEntries to all peers
        for (size_t i = 0; i < peerIps.size(); ++i) {
            if (peerIps[i] == ip) continue;
            AppendEntriesMessage msg;
            msg.term = currentTerm;
            msg.prevLogIndex = nextIndex[i] - 1;
            msg.prevLogTerm = (msg.prevLogIndex >= 0 && msg.prevLogIndex < (int)log.size()) ? log[msg.prevLogIndex].term : 0;
            msg.leaderCommit = commitIndex;
            msg.entryCount = 0;
            // Send up to 1 entry for simplicity
            if (static_cast<size_t>(nextIndex[i]) < log.size()) {
                msg.entries[0] = log[nextIndex[i]];
                msg.entryCount = 1;
            }
            send_msg(peerIps[i], &msg, sizeof(msg));
        }

        // Wait for majority of responses or timeout
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(150), [this] { return is_majority(); });

        // Advance commitIndex if possible
        for (int N = log.size() - 1; N > commitIndex; --N) {
            int count = 1; // Leader itself
            for (size_t i = 0; i < matchIndex.size(); ++i)
                if (matchIndex[i] >= N) ++count;
            if (count > peerIps.size() / 2 && log[N].term == currentTerm) {
                commitIndex = N;
                break;
            }
        }

        // Apply committed entries
        while (lastApplied < commitIndex) {
            ++lastApplied;
            // Apply log[lastApplied] to state machine (not shown)
        }
    }
}

bool Node::is_majority() {
    return nodesReceived > peerIps.size() / 2;
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

void Node::listen() {
    serverSock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(serverSock, (sockaddr*)&addr, sizeof(addr));
    Message msg;
    while (true) {
        Message msg;
        if (recv(serverSock, &msg, sizeof(msg), MSG_PEEK) == 0) continue;
        spdlog::info("Received message");
        std::lock_guard<std::mutex> lock{mtx};
        if (msg.term > currentTerm) {
            currentTerm = msg.term;
            role = Follower;
            votedFor = std::nullopt;
            reset_timeout();
        }
        if (msg.type == RequestVote) handle_request_vote();
        else if (msg.type == AppendEntriesReceived) handle_received();
        else if (msg.type == Received) nodesReceived++;
        else if (msg.type == AppendEntries) handle_append_entries();
        cv.notify_all();
    }
    close(serverSock);
}

void Node::handle_request_vote() {
    spdlog::info("Handling RequestVote");
    sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    RequestVoteMessage msg;
    if (recvfrom(serverSock, &msg, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&from), &fromlen) == 0) return;
    if ((!votedFor.load().has_value() || votedFor.load() == from.sin_addr.s_addr) && msg.term >= currentTerm) {
        votedFor = from.sin_addr.s_addr;
        reset_timeout();
        ReceivedMessage msg;
        send_msg(from.sin_addr.s_addr, &msg, sizeof(msg));
    }
}

void Node::handle_received() {
    spdlog::info("Handling AppendEntriesResponseMessage");
    // This is now used for AppendEntriesResponseMessage
    AppendEntriesResponseMessage msg;
    sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    if (recvfrom(serverSock, &msg, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&from), &fromlen) <= 0) return;

    std::lock_guard<std::mutex> lock(mtx);

    // Find peer index
    int peerIdx = -1;
    for (size_t i = 0; i < peerIps.size(); ++i)
        if (peerIps[i] == from.sin_addr.s_addr) peerIdx = i;
    if (peerIdx == -1) return;

    if (msg.success) {
        matchIndex[peerIdx] = msg.matchIndex;
        nextIndex[peerIdx] = msg.matchIndex + 1;
    } else {
        nextIndex[peerIdx] = std::max(1, nextIndex[peerIdx] - 1);
    }
    nodesReceived++;
    cv.notify_all();
}

void Node::handle_append_entries() {
    spdlog::info("Handling AppendEntries");
    reset_timeout();
    AppendEntriesMessage msg;
    sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    if (recvfrom(serverSock, &msg, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&from), &fromlen) <= 0) return;

    AppendEntriesResponseMessage response;
    response.term = currentTerm;
    response.success = false;
    response.matchIndex = -1;

    // Reply false if term < currentTerm
    if (msg.term < currentTerm) {
        spdlog::info("AppendEntries term < currentTerm");
        send_msg(from.sin_addr.s_addr, &response, sizeof(response));
        return;
    }

    // If log doesn't contain entry at prevLogIndex with prevLogTerm, fail
    if (msg.prevLogIndex >= 0) {
        spdlog::info("msg.prevLogIndex >= 0");
        if ((size_t)msg.prevLogIndex >= log.size() || log[msg.prevLogIndex].term != msg.prevLogTerm) {
            send_msg(from.sin_addr.s_addr, &response, sizeof(response));
            return;
        }
    }

    // Append any new entries not already in the log
    for (size_t i = 0; i < msg.entryCount; ++i) {
        int idx = msg.prevLogIndex + 1 + i;
        if ((size_t)idx < log.size()) {
            if (log[idx].term != msg.entries[i].term) {
                log.resize(idx); // Delete conflicting entry and all that follow
                log.push_back(msg.entries[i]);
            }
        } else {
            log.push_back(msg.entries[i]);
        }
    }

    // If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last new entry)
    if (msg.leaderCommit > commitIndex) {
        int lastNew = msg.prevLogIndex + msg.entryCount;
        commitIndex = std::min(msg.leaderCommit, lastNew);
    }

    response.success = true;
    response.matchIndex = msg.prevLogIndex + msg.entryCount;
    spdlog::info("Sending response to {}", from.sin_addr.s_addr);
    send_msg(from.sin_addr.s_addr, &response, sizeof(response));
}

void Node::reset_timeout() {
    static std::mt19937 rng;
    electionDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::uniform_int_distribution<size_t>{300, 500}(rng));
}
