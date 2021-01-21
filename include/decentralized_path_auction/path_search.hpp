#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <functional>

namespace decentralized_path_auction {

class PathSearch {
public:
    enum Error {
        SUCCESS = 0,
        INVALID_START_NODE,
        INVALID_GOAL_NODE,
        EMPTY_GOAL_NODES,
    };

    template <int Multiplier>
    static float linearRankCost(Graph::NodePtr, size_t rank) {
        return rank * Multiplier;
    }

    struct Config {
        std::string agent_id;
        std::function<float(Graph::NodePtr, size_t)> rank_cost = linearRankCost<10>;
        std::function<float(Graph::NodePtr, Graph::NodePtr, Graph::NodePtr)> traversal_cost =
                nullptr;
    };

    struct Path {
        struct Stop {
            Auction::Bid bid;
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
    friend class TestClass;

    void buildCollisionBids(const Auction::Bids&, float price);
    bool checkCollisionBids(const Auction::Bids&, float price);

    Graph _graph;
    Config _config;
    Graph _goal_nodes;
    Path _path;
    std::vector<Auction::Bid> _collision_bids_upper, _collision_bids_lower;
    size_t _search_id;
};

struct Auction::UserData {
    size_t search_id;
    float cost_estimate;
};

}  // namespace decentralized_path_auction
