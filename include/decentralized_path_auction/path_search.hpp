#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <functional>

namespace decentralized_path_auction {

class PathSearch {
public:
    enum Error {
        SUCCESS = 0,
        INVALID_CONFIG,
        INVALID_START_NODE,
        INVALID_GOAL_NODE,
        EMPTY_GOAL_NODES,
        EMPTY_GRAPH,
    };

    using TravelTime =
            std::function<float(const Graph::NodePtr& prev, const Graph::NodePtr& cur, const Graph::NodePtr& next)>;

    struct Config {
        std::string agent_id;
        float time_exchange_rate = 1;
        TravelTime travel_time = [](const Graph::NodePtr&, const Graph::NodePtr& cur, const Graph::NodePtr& next) {
            return bg::distance(cur->position, next->position);
        };

        bool validate() const { return !agent_id.empty() && time_exchange_rate > 0 && travel_time; }
    };

    struct Visit {
        Graph::NodePtr node;
        float price;
        float time;
    };

    using Path = std::vector<Visit>;

    PathSearch(Config config, Graph graph)
            : _config(std::move(config))
            , _graph(std::move(graph)) {}

    Error resetSearch(Graph::NodePtr, Graph::Nodes goal_nodes);
    Error iterateSearch();

    Graph& getGraph() { return _graph; }
    Config& getConfig() { return _config; }

private:
    Config _config;
    Graph _graph;
    Graph _goal_nodes;
    Path _path;
    size_t _search_id;
};

}  // namespace decentralized_path_auction
