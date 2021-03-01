#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <functional>

namespace decentralized_path_auction {

class PathSearch {
public:
    enum Error {
        SUCCESS,
        COST_LIMIT_EXCEEDED,
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
        CONFIG_COST_LIMIT_NON_POSITIVE,
        CONFIG_PRICE_INCREMENT_NON_POSITIVE,
        CONFIG_TIME_EXCHANGE_RATE_NON_POSITIVE,
        CONFIG_TRAVEL_TIME_MISSING,
    };

    using TravelTime = std::function<float(const NodePtr& prev, const NodePtr& cur, const NodePtr& next)>;

    struct Config {
        std::string agent_id;
        float cost_limit = FLT_MAX;
        float price_increment = 1;
        float time_exchange_rate = 1;
        TravelTime travel_time = travelDistance;

        Error validate() const;
    };

    PathSearch(Config config)
            : _config(std::move(config)) {}

    Error reset(Nodes destinations);
    Error iterate(Path& path, size_t iterations = 0);
    Error iterateFallback(Path& path, size_t iterations);  // divert when cost limit is exceeded

    Config& editConfig() { return _config; }

    static float travelDistance(const NodePtr&, const NodePtr& cur, const NodePtr& next) {
        return bg::distance(cur->position, next->position);
    }

private:
    float getCostEstimate(const NodePtr& node, float base_price, const Auction::Bid& bid);
    float findMinCostVisit(Visit& min_cost_visit, const Visit& visit, const Visit& front_visit);
    bool appendMinCostVisit(size_t visit_index, Path& path);
    bool detectCycle(const Auction::Bid& bid, const Visit& visit, const Visit& front_visit);
    bool checkCostLimit(const Visit& visit);
    bool checkTermination(const Visit& visit) const;
    float determinePrice(float base_price, float price_limit, float cost, float alternative_cost) const;

    Config _config;
    NodeRTree _dst_nodes;

    using BidKey = std::tuple<size_t, const Node*, float>;
    std::vector<std::pair<BidKey, float>> _cost_estimates, _fallback_cost_estimates;
    std::vector<CycleVisit> _cycle_visits;
    size_t _cycle_nonce = 0;
    size_t _search_nonce = 1;
};

}  // namespace decentralized_path_auction
