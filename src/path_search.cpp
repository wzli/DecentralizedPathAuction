#include <decentralized_path_auction/path_search.hpp>

#include <algorithm>
#include <cassert>

#define DEBUG_PRINTF(...)  // printf(__VA_ARGS__)

namespace decentralized_path_auction {

const Auction::Bid& baseBid(const Visit& visit) {
    return visit.node->auction.getBids().find(visit.base_price)->second;
}

PathSearch::Error PathSearch::Config::validate() const {
    if (agent_id.empty()) {
        return CONFIG_AGENT_ID_EMPTY;
    }
    if (price_increment <= 0) {
        return CONFIG_PRICE_INCREMENT_NON_POSITIVE;
    }
    if (time_exchange_rate <= 0) {
        return CONFIG_TIME_EXCHANGE_RATE_NON_POSITIVE;
    }
    if (!travel_time) {
        return CONFIG_TRAVEL_TIME_MISSING;
    }
    return SUCCESS;
}

PathSearch::Error PathSearch::reset(Graph::Nodes nodes) {
    // reset cost estimates
    _cost_estimates.clear();
    // reset destination nodes
    _dst_nodes.detachNodes();
    for (auto& node : nodes) {
        // verify each destination node
        if (!Graph::validateNode(node)) {
            return DESTINATION_NODE_INVALID;
        }
        if (node->state >= Graph::Node::NO_PARKING) {
            return DESTINATION_NODE_NO_PARKING;
        }
        if (!_dst_nodes.insertNode(std::move(node))) {
            return DESTINATION_NODE_DUPLICATED;
        }
    }
    return SUCCESS;
}

PathSearch::Error PathSearch::iterate(Path& path, size_t iterations) {
    // check configs
    if (Error config_error = _config.validate()) {
        return config_error;
    }
    // check source node
    if (path.empty()) {
        return SOURCE_NODE_NOT_PROVIDED;
    }
    auto& src_node = path.front().node;
    if (!Graph::validateNode(src_node)) {
        path.resize(1);
        return SOURCE_NODE_INVALID;
    }
    if (src_node->state >= Graph::Node::DISABLED) {
        path.resize(1);
        return SOURCE_NODE_DISABLED;
    }
    // check destination nodes (empty means passive and any node will suffice)
    if (!_dst_nodes.getNodes().empty() && !_dst_nodes.findAnyNode(Graph::Node::ENABLED)) {
        path.resize(1);
        return DESTINATION_NODE_NO_PARKING;
    }
    // source visit is required to have the highest bid in auction to claim the source node
    path.front().time = 0;
    path.front().price = std::numeric_limits<float>::max();
    path.front().base_price = src_node->auction.getHighestBid(_config.agent_id)->first;
    if (path.front().base_price >= std::numeric_limits<float>::max()) {
        path.resize(1);
        return SOURCE_NODE_OCCUPIED;
    }
    // allocate cost lookup
    _cost_estimates.resize(DenseId<Auction::Bid>::count());
    size_t original_path_size = path.size();
    // truncate visits in path that are invalid (node got deleted/disabled or bid got removed)
    path.erase(std::find_if(path.begin() + 1, path.end(),
                       [](const Visit& visit) {
                           return !Graph::validateNode(visit.node) || visit.node->state >= Graph::Node::DISABLED ||
                                  !visit.node->auction.getBids().count(visit.base_price) ||
                                  visit.time < (&visit - 1)->time;
                       }),
            path.end());
    if (checkTermination(path.back())) {
        return SUCCESS;
    }
    // iterate in reverse order through each visit in path on first pass
    for (int visit_index = path.size() - 1; visit_index >= 0; --visit_index) {
        // use index to iterate since path will be modified at the end of each loop
        appendMinCostVisit(visit_index, path);
    }
    if (iterations == 0) {
        return path.size() > original_path_size ? PATH_EXTENDED : PATH_CONTRACTED;
    }
    // run through requested iterations
    for (size_t visit_index = path.size() - 1; iterations--;
            // if cost increased check previous visit, otherwise start again from last visit
            visit_index = (appendMinCostVisit(visit_index, path) && visit_index > 0 ? visit_index : path.size()) - 1) {
        if (checkTermination(path[visit_index])) {
            return SUCCESS;
        }
    }
    return ITERATIONS_REACHED;
}

float PathSearch::getCostEstimate(const Graph::NodePtr& node, const Auction::Bid& bid) {
    auto& [cost_bid, cost_estimate] = _cost_estimates[bid.id];
    if (cost_bid != &bid) {
        cost_bid = &bid;
        // initialize cost proportional to travel time from node to destination
        if (_dst_nodes.getNodes().empty()) {
            cost_estimate = 0;
        } else {
            assert(Graph::validateNode(node));
            auto nearest_goal = _dst_nodes.findNearestNode(node->position, Graph::Node::ENABLED);
            cost_estimate = _config.travel_time(nullptr, node, nearest_goal) * _config.time_exchange_rate;
        }
    }
    return cost_estimate;
}

float PathSearch::findMinCostVisit(Visit& min_cost_visit, const Visit& visit, const Visit& start_visit) {
    const Graph::NodePtr& prev_node = &visit == &start_visit ? nullptr : (&visit - 1)->node;
    float min_cost = std::numeric_limits<float>::max();
    min_cost_visit = {nullptr};
    // loop over each adjacent node
    for (auto& adj_node : visit.node->edges) {
        DEBUG_PRINTF("->[%f %f] \r\n", adj_node->position.x(), adj_node->position.y());
        // skip disabled nodes
        if (adj_node->state >= Graph::Node::DISABLED) {
            DEBUG_PRINTF("Node Disabled\r\n");
            continue;
        }
        // calculate the expected time to arrive at the adjacent node (without wait)
        float earliest_arrival_time = visit.time + _config.travel_time(prev_node, visit.node, adj_node);
        assert(earliest_arrival_time > visit.time && "travel time must be positive");
        float wait_duration = 0;
        // iterate in reverse order (highest to lowest) through each bid in the auction of the adjacent node
        auto& adj_bids = adj_node->auction.getBids();
        for (auto adj_bid = adj_bids.rbegin(); adj_bid != adj_bids.rend(); ++adj_bid) {
            // wait duration is atleast as long as the total duration of next higher bid
            if (adj_bid != adj_bids.rbegin() && std::prev(adj_bid)->second.bidder != _config.agent_id) {
                wait_duration = std::max(wait_duration, std::prev(adj_bid)->second.totalDuration());
            }
            auto& [bid_price, bid] = *adj_bid;
            DEBUG_PRINTF("    ID %lu Base %f ", bid.id.id, bid_price);
            // skip bids that belong to this agent
            if (bid.bidder == _config.agent_id) {
                DEBUG_PRINTF("Skipped Self\r\n");
                continue;
            }
            // skip bid if it requires waiting but current node doesn't allow it
            if (visit.node->state == Graph::Node::NO_STOPPING && wait_duration > earliest_arrival_time) {
                DEBUG_PRINTF("No Stopping\r\n");
                continue;
            }
            // skip the bid if it causes cyclic dependencies
            if (detectCycle(bid, visit, start_visit)) {
                DEBUG_PRINTF("Cycle Detected\r\n");
                continue;
            }
            // arrival_time factors in how long you have to wait
            float arrival_time = std::max(wait_duration, earliest_arrival_time);
            // time cost is proportinal to duration between arrival and departure
            float time_cost = (arrival_time - visit.time) * _config.time_exchange_rate;
            assert(time_cost >= 0);
            // total cost of visit aggregates time cost, price of bid, and estimated cost from adj node to dst
            float cost_estimate = time_cost + bid_price + getCostEstimate(adj_node, bid);
            DEBUG_PRINTF("Cost %f\r\n", cost_estimate);
            // keep track of lowest cost visit found
            if (cost_estimate < min_cost) {
                std::swap(cost_estimate, min_cost);
                min_cost_visit.node = adj_node;
                min_cost_visit.time = arrival_time;
                min_cost_visit.base_price = bid_price;
            }
            // store second best cost in the price field
            min_cost_visit.price = std::min(cost_estimate, min_cost_visit.price);
        }
    }
    // decide on a bid price for the min cost visit
    if (min_cost_visit.node) {
        auto higher_bid = min_cost_visit.node->auction.getHigherBid(min_cost_visit.base_price, _config.agent_id);
        min_cost_visit.price = determinePrice(min_cost_visit.base_price,
                higher_bid == min_cost_visit.node->auction.getBids().end() ? std::numeric_limits<float>::max()
                                                                           : higher_bid->first,
                min_cost, min_cost_visit.price);
    }
    return min_cost;
}

bool PathSearch::appendMinCostVisit(size_t visit_index, Path& path) {
    assert(visit_index < path.size());
    auto& visit = path[visit_index];
    // remove edges to deleted nodes
    auto& edges = visit.node->edges;
    edges.erase(std::remove_if(edges.begin(), edges.end(), std::not_fn(Graph::validateNode)), edges.end());
    DEBUG_PRINTF("[%f %f] ID %lu Base %f\r\n", visit.node->position.x(), visit.node->position.y(), baseBid(visit).id.id,
            visit.base_price);
    // find min cost visit
    Visit min_cost_visit;
    float min_cost = findMinCostVisit(min_cost_visit, visit, path.front());
    // update cost estimate of current visit to the min cost of all adjacent visits
    auto& bid = baseBid(visit);
    auto& [cost_bid, cost_estimate] = _cost_estimates[bid.id];
    bool cost_increased = min_cost > cost_estimate;
    DEBUG_PRINTF("Min ID %lu Base %f Cost %f Prev %f\r\n\r\n", min_cost_visit.node ? baseBid(min_cost_visit).id.id : -1,
            min_cost_visit.base_price, min_cost, cost_estimate);
    cost_estimate = min_cost;
    cost_bid = &bid;
    if (!min_cost_visit.node) {
        // truncate rest of path if min cost visit is a dead end
        path.resize(visit_index + 1);
    } else if (&visit == &path.back()) {
        // append min cost visit if already at the back of path
        path.push_back(std::move(min_cost_visit));
    } else {
        // truncate path if min cost visit is different from next visit in path
        auto& next_visit = path[visit_index + 1];
        if (next_visit.node != min_cost_visit.node || next_visit.base_price != min_cost_visit.base_price) {
            path.resize(visit_index + 2);
        }
        // set next visit to the min cost visit found
        next_visit = std::move(min_cost_visit);
    }
    // WARNING: visit ref variable is invalidated at this point after modifying path vector
    return cost_increased;
}

bool PathSearch::detectCycle(const Auction::Bid& bid, const Visit& visit, const Visit& start_visit) {
    _cycle_visits.clear();
    for (const Visit* visit_ptr = &start_visit; visit_ptr != &visit; ++visit_ptr) {
        auto idx = 2 * baseBid(*visit_ptr).id;
        _cycle_visits.resize(std::max(_cycle_visits.size(), idx + 2));
        _cycle_visits[idx] = true;
    }
    auto& base_bid = baseBid(visit);
    _cycle_visits.resize(std::max({_cycle_visits.size(), (bid.id + 1) * 2, (base_bid.id + 1) * 2}));
    _cycle_visits[2 * bid.id] = true;
    if (base_bid.detectCycle(_cycle_visits, _config.agent_id)) {
        return true;
    }
    _cycle_visits[2 * bid.id] = false;
    return bid.detectCycle(_cycle_visits, _config.agent_id);
}

bool PathSearch::checkTermination(const Visit& visit) const {
    // termination condition for passive paths (any parkable node where there are no other bids)
    return (_dst_nodes.getNodes().empty() && visit.node->state < Graph::Node::NO_PARKING &&
                   visit.node->auction.getHighestBid(_config.agent_id)->second.bidder.empty()) ||
           // termination condition for regular destinations
           _dst_nodes.containsNode(visit.node);
}

float PathSearch::determinePrice(float base_price, float price_limit, float cost, float alternative_cost) const {
    assert(cost <= alternative_cost);
    assert(base_price <= price_limit);
    float min_price = base_price + _config.price_increment;
    float mid_price = (base_price + price_limit) / 2;
    if (mid_price <= min_price) {
        return mid_price;
    }
    // willing to pay additionally up to the surplus benefit compared to best alternative
    float price = base_price + alternative_cost - cost;
    if (price <= min_price || (alternative_cost >= std::numeric_limits<float>::max() &&
                                      price_limit >= std::numeric_limits<float>::max())) {
        return min_price;
    }
    return std::min(price, price_limit - _config.price_increment);
}

}  // namespace decentralized_path_auction
