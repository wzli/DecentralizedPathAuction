#include <decentralized_path_auction/path_search.hpp>

#include <algorithm>
#include <cassert>

#define DEBUG_PRINTF(...)  // printf(__VA_ARGS__)

namespace decentralized_path_auction {

static inline const Auction::Bid& baseBid(const Visit& visit) {
    return visit.node->auction.getBids().find(visit.base_price)->second;
}

PathSearch::Error PathSearch::Config::validate() const {
    if (agent_id.empty()) {
        return CONFIG_AGENT_ID_EMPTY;
    }
    if (cost_limit <= 0) {
        return CONFIG_COST_LIMIT_NON_POSITIVE;
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

PathSearch::Error PathSearch::reset(Nodes destinations) {
    // reset cost estimates
    ++_search_nonce;
    // reset destination nodes
    _dst_nodes.clearNodes();
    for (auto& node : destinations) {
        // verify each destination node
        if (!Node::validate(node)) {
            return DESTINATION_NODE_INVALID;
        }
        if (node->state >= Node::NO_PARKING) {
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
    if (!Node::validate(src_node)) {
        return SOURCE_NODE_INVALID;
    }
    if (src_node->state >= Node::DISABLED) {
        return SOURCE_NODE_DISABLED;
    }
    // check destination nodes (empty means passive and any node will suffice)
    if (!_dst_nodes.getNodes().empty() && !_dst_nodes.findAnyNode(Node::DEFAULT)) {
        return DESTINATION_NODE_NO_PARKING;
    }
    // source visit is required to have the highest bid in auction to claim the source node
    path.front().time = 0;
    path.front().base_price = src_node->auction.getHighestBid(_config.agent_id)->first;
    if (path.front().base_price >= FLT_MAX) {
        return SOURCE_NODE_OCCUPIED;
    }
    path.front().price = determinePrice(path.front().base_price, FLT_MAX, 0, FLT_MAX);
    // trivial solution
    if (checkTermination(path.front())) {
        path.resize(1);
        return SUCCESS;
    }
    // allocate cost lookup
    _cost_estimates.resize(DenseId<Auction::Bid>::count());
    _cycle_visits.resize(DenseId<Auction::Bid>::count());
    size_t original_path_size = path.size();
    // truncate visits in path that are invalid (node got deleted/disabled or bid got removed)
    path.erase(std::find_if(path.begin() + 1, path.end(),
                       [this](const Visit& visit) {
                           return !Node::validate(visit.node) || visit.node->state >= Node::DISABLED ||
                                  !visit.node->auction.getBids().count(visit.base_price) ||
                                  visit.time < (&visit - 1)->time || checkTermination(visit);
                       }),
            path.end());
    // iterate in reverse order through each visit in path on first pass
    // use index to iterate since path will be modified at the end of each loop
    for (int visit_index = path.size() - 1; visit_index >= 0; --visit_index) {
        appendMinCostVisit(visit_index, path);
    }
    if (checkCostLimit(path.front())) {
        return COST_LIMIT_EXCEEDED;
    }
    if (checkTermination(path.back())) {
        return SUCCESS;
    }
    if (iterations == 0) {
        return path.size() > original_path_size ? PATH_EXTENDED : PATH_CONTRACTED;
    }
    // run through requested iterations
    for (size_t visit_index = path.size() - 1; iterations--; --visit_index) {
        DEBUG_PRINTF("IDX %lu ITT %lu\r\n", visit_index, iterations);
        // check previous visit if cost increased otherwise start again from last visit
        if (!appendMinCostVisit(visit_index, path) || visit_index == 0) {
            if (checkCostLimit(path.front())) {
                return COST_LIMIT_EXCEEDED;
            }
            if (checkTermination(path.back())) {
                return SUCCESS;
            }
            visit_index = path.size();
        }
    }
    return ITERATIONS_REACHED;
}

PathSearch::Error PathSearch::iterate(Path& path, size_t iterations, float fallback_cost) {
    if (fallback_cost < 0) {
        return FALLBACK_COST_NEGATIVE;
    }
    // calculate fallback path by swapping out destination and cost estimates
    auto dst_nodes = std::move(_dst_nodes);
    assert(_dst_nodes.getNodes().empty());
    _cost_estimates.swap(_fallback_cost_estimates);
    auto fallback_error = iterate(path, iterations);
    _dst_nodes = std::move(dst_nodes);
    _cost_estimates.swap(_fallback_cost_estimates);
    // return fallback if search input check failed or destination was empty
    if (fallback_error > ITERATIONS_REACHED || _dst_nodes.getNodes().empty()) {
        return fallback_error;
    }
    // cost of requested path should not exceed cost of fallback option
    float cost_limit = std::min(path.front().cost_estimate + fallback_cost, _config.cost_limit);
    std::swap(_config.cost_limit, cost_limit);
    Path requested_path = {path.front()};
    auto error = iterate(requested_path, iterations);
    std::swap(_config.cost_limit, cost_limit);
    // return requested path if successfully found
    if (error == SUCCESS) {
        path = std::move(requested_path);
        return SUCCESS;
    }
    // return fallback path if succesfully found
    if (fallback_error == SUCCESS) {
        return FALLBACK_DIVERTED;
    }
    // stay if one place if both requested and fallback paths fail
    path.resize(1);
    return error;
}

float PathSearch::getCostEstimate(const NodePtr& node, float base_price, const Auction::Bid& bid) {
    auto& [key, cost_estimate] = _cost_estimates[bid.id];
    BidKey new_key = {_search_nonce, node.get(), base_price};
    if (key != new_key) {
        key = new_key;
        // initialize cost proportional to travel time from node to destination
        if (_dst_nodes.getNodes().empty()) {
            cost_estimate = 0;
        } else {
            assert(Node::validate(node));
            auto nearest_goal = _dst_nodes.findNearestNode(node->position, Node::DEFAULT);
            cost_estimate = _config.travel_time(nullptr, node, nearest_goal) * _config.time_exchange_rate;
        }
    }
    return cost_estimate;
}

float PathSearch::findMinCostVisit(Visit& min_cost_visit, const Visit& visit, const Visit& front_visit) {
    const NodePtr& prev_node = &visit == &front_visit ? nullptr : (&visit - 1)->node;
    float min_cost = FLT_MAX;
    min_cost_visit = {nullptr, 0, min_cost};
    // loop over each adjacent node
    for (const auto& adj_node : visit.node->edges) {
        DEBUG_PRINTF("->[%f %f] \r\n", adj_node->position.x(), adj_node->position.y());
        // skip disabled nodes
        if (adj_node->state >= Node::DISABLED) {
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
            auto& [bid_price, bid] = *adj_bid;
            DEBUG_PRINTF("    ID %lu Base %f ", bid.id(), bid_price);
            // check if higher bid exists
            if (adj_bid != adj_bids.rbegin() && std::prev(adj_bid)->second.bidder != _config.agent_id) {
                auto& [higher_price, higher_bid] = *std::prev(adj_bid);
                // wait duration is atleast as long as the total duration of next higher bid
                wait_duration = std::max(wait_duration, higher_bid.totalDuration());
                // skip if there is no price gap between base bid and next higher bid
                if (std::nextafter(bid_price, FLT_MAX) >= higher_price) {
                    DEBUG_PRINTF("No Price Gap\r\n");
                    continue;
                }
            }
            // skip bids that belong to this agent
            if (bid.bidder == _config.agent_id) {
                DEBUG_PRINTF("Skipped Self\r\n");
                continue;
            }
            // skip bid if it requires waiting but current node doesn't allow it
            if (visit.node->state == Node::NO_STOPPING && wait_duration > earliest_arrival_time) {
                DEBUG_PRINTF("No Stopping\r\n");
                continue;
            }
            // skip the bid if it causes cyclic dependencies
            if (detectCycle(bid, visit, front_visit)) {
                DEBUG_PRINTF("Cycle Detected\r\n");
                continue;
            }
            // arrival_time factors in how long you have to wait
            float arrival_time = std::max(wait_duration, earliest_arrival_time);
            // time cost is proportinal to duration between arrival and departure
            float time_cost = (arrival_time - visit.time) * _config.time_exchange_rate;
            assert(time_cost >= 0);
            // total cost of visit aggregates time cost, price of bid, and estimated cost from adj node to dst
            float adj_cost = getCostEstimate(adj_node, bid_price, bid);
            float cost_estimate = time_cost + bid_price + adj_cost;
            DEBUG_PRINTF("Cost %f\r\n", cost_estimate);
            // keep track of lowest cost visit found
            if (cost_estimate < min_cost) {
                std::swap(cost_estimate, min_cost);
                min_cost_visit = {adj_node, arrival_time, min_cost_visit.price, bid_price, adj_cost};
            }
            // store second best cost in the price field
            min_cost_visit.price = std::min(cost_estimate, min_cost_visit.price);
        }
    }
    // decide on a bid price for the min cost visit
    if (min_cost_visit.node) {
        auto higher_bid = min_cost_visit.node->auction.getHigherBid(min_cost_visit.base_price, _config.agent_id);
        float higher_price = higher_bid == min_cost_visit.node->auction.getBids().end() ? FLT_MAX : higher_bid->first;
        min_cost_visit.price = determinePrice(min_cost_visit.base_price, higher_price, min_cost, min_cost_visit.price);
        assert(min_cost_visit.price > min_cost_visit.base_price);
        assert(min_cost_visit.price < higher_price);
    }
    return min_cost;
}

bool PathSearch::appendMinCostVisit(size_t visit_index, Path& path) {
    assert(visit_index < path.size());
    auto& visit = path[visit_index];
    // remove edges to deleted nodes
    auto& edges = visit.node->edges;
    edges.erase(std::remove_if(edges.begin(), edges.end(), std::not_fn(Node::validate)), edges.end());
    DEBUG_PRINTF("[%f %f] ID %lu Base %f\r\n", visit.node->position.x(), visit.node->position.y(), baseBid(visit).id(),
            visit.base_price);
    // find min cost visit
    Visit min_cost_visit;
    float min_cost = findMinCostVisit(min_cost_visit, visit, path.front());
    // update cost estimate of current visit to the min cost of all adjacent visits
    auto& [cost_key, cost_estimate] = _cost_estimates[baseBid(visit).id];
    bool cost_increased = min_cost > cost_estimate;
    DEBUG_PRINTF("Min ID %lu Base %f Cost %f Prev %f\r\n\r\n", min_cost_visit.node ? baseBid(min_cost_visit).id() : -1,
            min_cost_visit.base_price, min_cost, cost_estimate);
    visit.cost_estimate = min_cost;
    cost_estimate = min_cost;
    cost_key = {_search_nonce, visit.node.get(), visit.base_price};
    const auto is_min_cost_visit = [&min_cost_visit](const Visit& v) {
        return v.node == min_cost_visit.node && v.base_price == min_cost_visit.base_price;
    };
    if (!min_cost_visit.node || std::any_of(path.begin(), path.begin() + visit_index, is_min_cost_visit)) {
        // truncate rest of path if min cost visit is a dead end or has a loop back
        path.resize(visit_index + 1);
    } else if (&visit == &path.back()) {
        // append min cost visit if already at the back of path
        path.push_back(std::move(min_cost_visit));
    } else {
        // truncate path if min cost visit is different from next visit in path
        auto& next_visit = path[visit_index + 1];
        if (!is_min_cost_visit(next_visit)) {
            path.resize(visit_index + 2);
        }
        next_visit = std::move(min_cost_visit);
    }
    // WARNING: visit ref variable is invalidated at this point after modifying path vector
    return cost_increased;
}

bool PathSearch::detectCycle(const Auction::Bid& bid, const Visit& visit, const Visit& front_visit) {
    ++_cycle_nonce;
    // mark previous visits in path as part of ancestor visits
    for (const Visit* visit_ptr = &front_visit; visit_ptr != &visit; ++visit_ptr) {
        _cycle_visits[baseBid(*visit_ptr).id] = {_cycle_nonce, 2};
    }
    // detect cycle of prev->lower bid
    auto& base_bid = baseBid(visit);
    _cycle_visits[bid.id] = {_cycle_nonce, 2};
    if (base_bid.detectCycle(_cycle_visits, _cycle_nonce, _config.agent_id)) {
        return true;
    }
    // detect cycle of lower bid
    _cycle_visits[base_bid.id].in_cycle = 2;
    _cycle_visits[bid.id].nonce = _cycle_nonce - 1;
    return bid.detectCycle(_cycle_visits, _cycle_nonce, _config.agent_id);
}

bool PathSearch::checkCostLimit(const Visit& visit) {
    return visit.base_price + visit.cost_estimate > _config.cost_limit;
}

bool PathSearch::checkTermination(const Visit& visit) const {
    // termination condition for passive paths (any parkable node where there are no other bids)
    return (_dst_nodes.getNodes().empty() && visit.node->state < Node::NO_PARKING &&
                   visit.node->auction.getHighestBid(_config.agent_id)->second.bidder.empty()) ||
           // termination condition for regular destinations
           _dst_nodes.containsNode(visit.node);
}

float PathSearch::determinePrice(float base_price, float price_limit, float cost, float alternative_cost) const {
    assert(cost <= alternative_cost);
    assert(base_price <= price_limit);
    base_price = std::nextafter(base_price, FLT_MAX);
    // take mid price if it is lower than minimum increment to avoid bidding over slot limit
    float min_price = base_price + _config.price_increment;
    float mid_price = (base_price + price_limit) / 2;
    if (mid_price <= min_price) {
        return mid_price;
    }
    // willing to pay additionally up to the surplus benefit compared to best alternative
    float price = base_price + alternative_cost - cost;
    if (price <= min_price || (alternative_cost >= FLT_MAX && price_limit >= FLT_MAX)) {
        return min_price;
    }
    return std::min(price, price_limit - _config.price_increment);
}

}  // namespace decentralized_path_auction
