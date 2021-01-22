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

    using TravelTime = std::function<float(
            const Graph::NodePtr& prev, const Graph::NodePtr& cur, const Graph::NodePtr& next)>;

    struct Config {
        std::string agent_id;
        TravelTime travel_time = [](const Graph::NodePtr&, const Graph::NodePtr& cur,
                                         const Graph::NodePtr& next) {
            return bg::distance(cur->position, next->position);
        };
    };

    struct Path {
        struct Stop {
            Graph::NodePtr node;
            float price;
            float travel_time;
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

    bool checkCollision(const Auction::Bids& from_bids, const Auction::Bids& to_bids,
            float from_price, float to_price) const;

private:
    Graph _graph;
    Config _config;
    Graph _goal_nodes;
    Path _path;
    size_t _search_id;
};

}  // namespace decentralized_path_auction
