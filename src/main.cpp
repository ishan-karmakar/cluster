#include <iostream>
#include <vector>
#include "raft.h"
#include <spdlog/spdlog.h>

int main() {
    spdlog::set_level(spdlog::level::debug);
    // raft::Node{"10.5.0.2", "10.5.0.3", "10.5.0.4"}();
    raft::Node{"raft-node-1", "raft-node-2", "raft-node-3"}();

    return 0;
}