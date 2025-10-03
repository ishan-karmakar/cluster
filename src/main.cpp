#include "safehouse/raft.h"
#include "spdlog/spdlog.h"

int main() {
    safehouse::cluster::Node{"raft-node-1", "raft-node-2", "raft-node-3"}();
    spdlog::info("Hello World");
    return 0;
}