#include <iostream>
#include <vector>
#include "raft.h"
#include <spdlog/spdlog.h>

int main() {
    spdlog::set_level(spdlog::level::debug);
    raft::Node{"raft-node-1", "raft-node-2", "raft-node-3"}();

    return 0;
}