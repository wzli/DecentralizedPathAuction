#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <functional>

namespace decentralized_path_auction {

class PathSearch {
public:
    enum Error {
        SUCCESS = 0,

        GRAPH_EMPTY,

        PATH_EXTENDED,
        PATH_CONTRACTED,

        DESTINATION_NODE_NOT_FOUND,
        DESTINATION_NODE_NOT_ENABLED,
        DESTINATION_NODE_DUPLICATED,

        SOURCE_NODE_NOT_PROVIDED,
        SOURCE_NODE_NOT_FOUND,
        SOURCE_NODE_DISABLED,

        CONFIG_AGENT_ID_EMPTY,
        CONFIG_TIME_EXCHANGE_RATE_NON_POSITIVE,
        CONFIG_TRAVEL_TIME_MISSING,
    };

    using TravelTime =
            std::function<float(const Graph::NodePtr& prev, const Graph::NodePtr& cur, const Graph::NodePtr& next)>;

    static float travelDistance(const Graph::NodePtr&, const Graph::NodePtr& cur, const Graph::NodePtr& next) {
        return bg::distance(cur->position, next->position);
    }

    struct Config {
        std::string agent_id;
        float time_exchange_rate = 1;
        TravelTime travel_time = travelDistance;

        Error validate() const;
    };

    struct Visit {
        Graph::NodePtr node;
        float time = 0;                                   // expected time of arrival
        float price = 0;                                  // current bid price of the slot
        float value = std::numeric_limits<float>::max();  // amount willing to pay for the slot
    };

    using Path = std::vector<Visit>;

    PathSearch(Config config, Graph graph)
            : _config(std::move(config))
            , _graph(std::move(graph)) {}

    Error setDestination(Graph::Nodes nodes);
    Error iterateSearch(Path& path);

    Graph& getGraph() { return _graph; }
    Config& getConfig() { return _config; }

private:
    bool detectCycle(const Auction::Bid& bid, const Path& path);
    float getCostEstimate(const Graph::NodePtr& node, const Auction::Bid& bid) const;

    Config _config;
    Graph _graph;
    Graph _dst_nodes;
    Path _path;
    size_t _cycle_nonce = 0;
    size_t _cost_nonce = 0;
};

}  // namespace decentralized_path_auction
