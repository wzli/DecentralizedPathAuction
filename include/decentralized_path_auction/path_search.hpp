#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <functional>

namespace decentralized_path_auction {

struct Auction::UserData {
    size_t search_id;
    float cost_estimate;
};

class PathSearch {
public:
    enum Error {
        SUCCESS = 0,
        INVALID_START_NODE,
        INVALID_GOAL_NODE,
        EMPTY_GOAL_NODES,
    };

    struct Config {
        std::string agent_id;
        std::function<float(Graph::NodePtr, Graph::NodePtr, Graph::NodePtr)> traversal_cost;
        std::function<float(int)> rank_cost;
    };

    struct Path {
        struct Stop {
            Auction::Bid reference_bid;
            Graph::NodePtr node;
        };

        std::vector<Stop> stops;
    };

    PathSearch(Graph graph, Config config)
            : _graph(std::move(graph))
            , _config(std::move(config)) {}

    Error resetSearch(Graph::NodePtr, Graph::Nodes goal_nodes);
    Error iterateSearch();

    Graph& getGraph() { return _graph; }
    Config& getConfig() { return _config; }

private:
    void buildCollisionBlacklist(const Auction&, float price);

    Graph _graph;
    Config _config;
    Graph _goal_nodes;
    Path _path;
    std::vector<Auction::Bidder> _collision_blacklist_upper, _collision_blacklist_lower;
    size_t _search_id;
};

}  // namespace decentralized_path_auction
