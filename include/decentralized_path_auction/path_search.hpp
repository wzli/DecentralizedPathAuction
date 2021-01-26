#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <functional>

namespace decentralized_path_auction {

class PathSearch {
public:
    enum Error {
        SUCCESS = 0,
        EXTENDED_PATH,
        CONTRACTED_PATH,
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
        float price;  // current bid price of the slot
        float value;  // amount willing to pay for the slot
        float time;   // expected time of arrival
    };

    using Path = std::vector<Visit>;

    PathSearch(Config config, Graph graph)
            : _config(std::move(config))
            , _graph(std::move(graph)) {}

    // keep previous destinations by leaving dst_nodes empty
    Error resetSearch(Graph::NodePtr src_node, Graph::Nodes dst_nodes = {});
    Error iterateSearch();

    Graph& getGraph() { return _graph; }
    Config& getConfig() { return _config; }

private:
    float getCostEstimate(const Graph::NodePtr& node, const Auction::Bid& bid) const;
    bool checkCollision(const Auction::Bid& bid, int visit_index);

    Config _config;
    Graph _graph;
    Graph _dst_nodes;
    Graph::NodePtr _src_node;
    Path _path;
    size_t _search_id = 0;
    size_t _collision_id = 0;
};

}  // namespace decentralized_path_auction
