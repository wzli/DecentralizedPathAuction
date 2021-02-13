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
            : _config(std::move(config)) {}

    Error reset(Graph::Nodes nodes);
    Error iterate(Path& path, size_t iterations = 0);

    Config& editConfig() { return _config; }

    static float travelDistance(const Graph::NodePtr&, const Graph::NodePtr& cur, const Graph::NodePtr& next) {
        return bg::distance(cur->position, next->position);
    }

private:
    float getCostEstimate(const Graph::NodePtr& node, const Auction::Bid& bid);
    float findMinCostVisit(Visit& min_cost_visit, const Visit& visit, const Path& path);
    bool appendMinCostVisit(size_t visit_index, Path& path);
    bool checkTermination(const Visit& visit) const;
    float determinePrice(float base_price, float price_limit, float cost, float alternative_cost) const;

    Config _config;
    Graph _dst_nodes;
    std::vector<bool> _cycle_visits;
    std::vector<std::pair<const Auction::Bid*, float>> _cost_estimates;
};

}  // namespace decentralized_path_auction
