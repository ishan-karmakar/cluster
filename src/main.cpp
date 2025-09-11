#include <iostream>
#include <vector>
#include "raft.h"
#include <spdlog/spdlog.h>

int main() {
    std::vector<std::string> peers = {
        "raft-node-1",
        "raft-node-2",
        "raft-node-3"
    };
    spdlog::set_level(spdlog::level::info);

    raft::Node{peers}.run();

    return 0;
}