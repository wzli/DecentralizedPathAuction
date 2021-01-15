#pragma once

#include <vector>

namespace decentralized_path_auction {

struct Checkpoint {
    Point2D position;
};

struct Route {
    std::vector<Checkpoint> checkpoints;
};

}  // namespace decentralized_path_auction
