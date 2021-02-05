#include <decentralized_path_auction/path_search.hpp>

#include <algorithm>
#include <cassert>

namespace decentralized_path_auction {

PathSearch::Error PathSearch::Config::validate() const {
    if (agent_id.empty()) {
        return CONFIG_AGENT_ID_EMPTY;
    }
    if (time_exchange_rate <= 0) {
        return CONFIG_TIME_EXCHANGE_RATE_NON_POSITIVE;
    }
    if (!travel_time) {
        return CONFIG_TRAVEL_TIME_MISSING;
    }
    return SUCCESS;
}

PathSearch::Error PathSearch::setDestination(Graph::Nodes nodes) {
    // only reset cost estimates when new destinations are added
    _cost_nonce += std::any_of(
            nodes.begin(), nodes.end(), [this](const Graph::NodePtr& node) { return !_dst_nodes.containsNode(node); });
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

PathSearch::Error PathSearch::iterateSearch(Path& path, size_t iterations) const {
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
    path.front().min_price = src_node->auction.getHighestBid(_config.agent_id)->first;
    size_t original_path_size = path.size();
    // truncate visits in path that are invalid (node got deleted/disabled or bid got removed)
    path.erase(std::find_if(path.begin() + 1, path.end(),
                       [](const Visit& visit) {
                           return !Graph::validateNode(visit.node) || visit.node->state >= Graph::Node::DISABLED ||
                                  !visit.node->auction.getBids().count(visit.min_price) ||
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

float PathSearch::findMinCostVisit(Visit& min_cost_visit, const Visit& visit, const Path& path) const {
    auto& prev_node = &visit == &path.front() ? nullptr : (&visit - 1)->node;
    float min_cost = std::numeric_limits<float>::max();
    min_cost_visit = {nullptr};
    // loop over each adjacent node
    for (auto& adj_node : visit.node->edges) {
        // skip disabled nodes
        if (adj_node->state >= Graph::Node::DISABLED) {
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
            // skip bids that belong to this agent
            if (bid.bidder == _config.agent_id) {
                continue;
            }
            // wait duration is atleast as long as the next total duration of next higher bid
            if (adj_bid != adj_bids.rbegin()) {
                wait_duration = std::max(wait_duration, std::prev(adj_bid)->second.totalDuration());
            }
            // skip bid if it requires waiting but current node doesn't allow it
            if (visit.node->state == Graph::Node::NO_STOPPING && wait_duration > earliest_arrival_time) {
                continue;
            }
            // skip the bid if it causes cyclic dependencies
            if (detectCycle(bid, path)) {
                continue;
            }
            // arrival_time factors in how long you have to wait
            float arrival_time = std::max(wait_duration, earliest_arrival_time);
            // time cost is proportinal to duration between arrival and departure
            float time_cost = (arrival_time - visit.time) * _config.time_exchange_rate;
            assert(time_cost >= 0);
            // total cost of visit aggregates time cost, price of bid, and estimated cost from adj node to dst
            float cost_estimate = time_cost + bid_price + getCostEstimate(adj_node, bid);
            // keep track of lowest cost visit found
            if (cost_estimate < min_cost) {
                min_cost = cost_estimate;
                min_cost_visit.node = std::move(adj_node);
                min_cost_visit.time = arrival_time;
                min_cost_visit.min_price = bid_price;
            } else {
                // store second best cost in the price field
                min_cost_visit.price = std::min(cost_estimate, min_cost_visit.price);
            }
        }
    }
    // decide on a bid price for the min cost visit
    auto higher_bid = visit.node->auction.getHigherBid(min_cost_visit.min_price, _config.agent_id);
    if (higher_bid == visit.node->auction.getBids().end()) {
        // no upper limit, willing to pay additionally up to the surplus benefit compared to 2nd best option
        min_cost_visit.price += min_cost_visit.min_price - min_cost;  // + 2nd best min cost (already stored)
    } else {
        // if upper limit is next highest bid, set price to midway between bids
        min_cost_visit.price = 0.5f * (min_cost_visit.min_price + higher_bid->first);
    }
    return min_cost;
}

bool PathSearch::appendMinCostVisit(size_t visit_index, Path& path) const {
    assert(visit_index < path.size());
    auto& visit = path[visit_index];
    // remove edges to deleted nodes
    auto& edges = visit.node->edges;
    edges.erase(std::remove_if(edges.begin(), edges.end(), std::not_fn(Graph::validateNode)), edges.end());
    // find min cost visit
    Visit min_cost_visit;
    float min_cost = findMinCostVisit(min_cost_visit, visit, path);
    // truncate rest of path and proceed to previous visit if current visit is a dead end
    if (!min_cost_visit.node) {
        path.resize(visit_index + 1);
    }
    // if next visit in path is not the min cost visit found
    else if (&visit == &path.back() || (&visit + 1)->node != min_cost_visit.node ||
             (&visit + 1)->min_price != min_cost_visit.min_price) {
        // truncate path upto current visit and append min cost visit to path
        path.resize(visit_index + 1);
        path.push_back(std::move(min_cost_visit));
    }
    // update cost estimate of current visit to the min cost of adjacent visits
    auto& bid = visit.node->auction.getBids().find(visit.min_price)->second;
    bool cost_increased = min_cost > bid.cost_estimate;
    bid.cost_estimate = min_cost;
    bid.cost_nonce = _cost_nonce;
    return cost_increased;
}

bool PathSearch::checkTermination(const Visit& visit) const {
    // termination condition for passive paths (any parkable node where there are no other bids)
    return (_dst_nodes.getNodes().empty() && visit.node->state < Graph::Node::NO_PARKING &&
                   visit.node->auction.getHighestBid(_config.agent_id)->second.bidder.empty()) ||
           // termination condition for regular destinations
           _dst_nodes.containsNode(visit.node);
}

bool PathSearch::detectCycle(const Auction::Bid& bid, const Path& path) const {
    ++_cycle_nonce;
    // set all previous visits in path as visited
    for (auto& visit : path) {
        assert(Graph::validateNode(visit.node));
        auto& visit_bid = visit.node->auction.getBids().find(visit.min_price)->second;
        visit_bid.cycle_nonce = _cycle_nonce;
        if (&visit_bid == &bid) {
            break;
        }
    }
    return bid.detectCycle(_cycle_nonce, _config.agent_id);
}

float PathSearch::getCostEstimate(const Graph::NodePtr& node, const Auction::Bid& bid) const {
    // reset cost estimate when nonce changes
    if (bid.cost_nonce != _cost_nonce) {
        bid.cost_nonce = _cost_nonce;
        // initialize cost proportional to travel time from node to destination
        if (_dst_nodes.getNodes().empty()) {
            bid.cost_estimate = 0;
        } else {
            assert(Graph::validateNode(node));
            auto nearest_goal = _dst_nodes.findNearestNode(node->position, Graph::Node::ENABLED);
            bid.cost_estimate = _config.travel_time(nullptr, node, nearest_goal) * _config.time_exchange_rate;
        }
    }
    return bid.cost_estimate;
}

}  // namespace decentralized_path_auction
