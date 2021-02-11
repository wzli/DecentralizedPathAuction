#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <functional>

namespace decentralized_path_auction {

class PathSearch {
public:
    enum Error {
        SUCCESS = 0,
        ITERATIONS_REACHED,
        PATH_EXTENDED,
        PATH_CONTRACTED,
        DESTINATION_NODE_INVALID,
        DESTINATION_NODE_NO_PARKING,
        DESTINATION_NODE_DUPLICATED,
        SOURCE_NODE_NOT_PROVIDED,
        SOURCE_NODE_INVALID,
        SOURCE_NODE_DISABLED,
        SOURCE_NODE_OCCUPIED,
        CONFIG_AGENT_ID_EMPTY,
        CONFIG_PRICE_INCREMENT_NON_POSITIVE,
        CONFIG_TIME_EXCHANGE_RATE_NON_POSITIVE,
        CONFIG_TRAVEL_TIME_MISSING,
    };

    using TravelTime =
            std::function<float(const Graph::NodePtr& prev, const Graph::NodePtr& cur, const Graph::NodePtr& next)>;

    struct Config {
        std::string agent_id;
        float price_increment = 1;
        float time_exchange_rate = 1;
        TravelTime travel_time = travelDistance;

        Error validate() const;
    };

    PathSearch(Config config)
            : _config(std::move(config))
            , _cost_nonce(std::hash<std::string>()(_config.agent_id))
            , _cycle_nonce(_cost_nonce) {}

    Error setDestination(Graph::Nodes nodes);
    Error iterateSearch(Path& path, size_t iterations = 0) const;

    Config& editConfig() { return _config; }

    static float travelDistance(const Graph::NodePtr&, const Graph::NodePtr& cur, const Graph::NodePtr& next) {
        return bg::distance(cur->position, next->position);
    }

private:
    float findMinCostVisit(Visit& min_cost_visit, const Visit& visit, const Path& path) const;
    bool appendMinCostVisit(size_t visit_index, Path& path) const;
    bool checkTermination(const Visit& visit) const;
    float getCostEstimate(const Graph::NodePtr& node, const Auction::Bid& bid) const;
    float determinePrice(float base_price, float price_limit, float cost, float alternative_cost) const;

    Config _config;
    Graph _dst_nodes;
    size_t _cost_nonce = 0;
    mutable size_t _cycle_nonce = 0;
};

}  // namespace decentralized_path_auction
