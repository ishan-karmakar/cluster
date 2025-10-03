#include "safehouse/cluster.h"

int main() {
    safehouse::cluster::Node{"raft-node-1", "raft-node-2", "raft-node-3"}();
    return 0;
}