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
    if (!_graph.findAnyNode(Graph::Node::DISABLED)) {
        return GRAPH_EMPTY;
    }
    // only reset cost estimates when new destinations are added
    _cost_nonce += std::any_of(
            nodes.begin(), nodes.end(), [this](const Graph::NodePtr& node) { return !_dst_nodes.containsNode(node); });
    // reset destination nodes
    _dst_nodes.detachNodes();
    for (auto& node : nodes) {
        // verify each destination node
        if (!_graph.containsNode(node)) {
            return DESTINATION_NODE_NOT_FOUND;
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

PathSearch::Error PathSearch::iterateSearch(Path& path) {
    // check configs
    if (Error config_error = _config.validate()) {
        return config_error;
    }
    // check source node
    if (path.empty()) {
        return SOURCE_NODE_NOT_PROVIDED;
    }
    auto& src_node = path.front().node;
    if (!_graph.containsNode(src_node)) {
        path.resize(1);
        return SOURCE_NODE_NOT_FOUND;
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
    size_t original_path_size = path.size();
    float src_price = src_node->auction.getHighestBid(_config.agent_id)->first;
    if (path.front().price != src_price) {
        path.front().price = src_price;
        path.resize(1);
    }
    // truncate visits in path that are invalid (node got deleted/disabled or bid got removed)
    path.erase(std::find_if(path.begin() + 1, path.end(),
                       [](const Visit& visit) {
                           return !Graph::validateNode(visit.node) || visit.node->state >= Graph::Node::DISABLED ||
                                  !visit.node->auction.getBids().count(visit.price) || visit.time < (&visit - 1)->time;
                       }),
            path.end());
    // termination condition for passive paths (any parkable node where there are no other bids)
    if ((_dst_nodes.getNodes().empty() && path.back().node->state < Graph::Node::NO_PARKING &&
                path.back().node->auction.getBids().size() == 1) ||
            // termination condition for regular destinations
            _dst_nodes.containsNode(path.back().node)) {
        return SUCCESS;
    }
    // iterate in reverse order through each visit in path
    for (int visit_index = path.size() - 1; visit_index >= 0; --visit_index) {
        // use index to iterate since path will be modified at the end of each loop
        auto& visit = path[visit_index];
        auto& bids = visit.node->auction.getBids();
        // store the min cost adjacent visit found
        float min_cost = std::numeric_limits<float>::max();
        Visit min_cost_visit{nullptr};
        // remove edges to deleted nodes
        auto& edges = visit.node->edges;
        edges.erase(std::remove_if(edges.begin(), edges.end(), std::not_fn(Graph::validateNode)), edges.end());
        // loop over each adjacent node
        for (auto& adj_node : edges) {
            // skip disabled nodes
            if (adj_node->state >= Graph::Node::DISABLED) {
                continue;
            }
            // calculate the expected time to arrive at the adjacent node (without wait)
            const auto& prev_node = visit_index == 0 ? nullptr : path[visit_index - 1].node;
            float earliest_arrival_time = visit.time + _config.travel_time(prev_node, visit.node, adj_node);
            assert(earliest_arrival_time >= 0);
            float wait_duration = 0;
            // iterate in reverse order (highest to lowest) through each bid in the auction of the adjacent node
            auto& adj_bids = adj_node->auction.getBids();
            for (auto adj_bid = adj_bids.rbegin(); adj_bid != adj_bids.rend(); ++adj_bid) {
                auto& [bid_price, bid] = *adj_bid;
                // skip bids that belong to this agent
                if (bid.bidder == _config.agent_id) {
                    continue;
                }
                // wait duration atleast as long as the next total duration of next highest bid in the auction
                if (adj_bid != adj_bids.rbegin()) {
                    wait_duration = std::max(wait_duration, std::prev(adj_bid)->second.totalDuration());
                }
                // skip bid if it requires waiting but previous node doesn't allow it
                if (visit.node->state == Graph::Node::NO_STOPPING && wait_duration > earliest_arrival_time) {
                    continue;
                }
                // skip the bid if it causes cyclic dependencies
                if (detectCycle(bid, path)) {
                    continue;
                }
                // arrival_time factors in how long you have to wait
                float arrival_time = std::max(wait_duration, earliest_arrival_time);
                // time cost is proportinal to difference in expected arrival time from current node to adjacent node
                float time_cost = (arrival_time - visit.time) * _config.time_exchange_rate;
                assert(time_cost >= 0);
                // total cost of visit aggregates time cost, price of bid, and estimated cost from adj node to dst
                float cost_estimate = time_cost + bid_price + getCostEstimate(adj_node, bid);
                // keep track of lowest cost visit found
                if (cost_estimate < min_cost) {
                    min_cost = cost_estimate;
                    min_cost_visit.node = std::move(adj_node);
                    min_cost_visit.time = arrival_time;
                    min_cost_visit.price = bid_price;
                } else {
                    // store second best cost in the value field
                    min_cost_visit.value = std::min(cost_estimate, min_cost_visit.value);
                }
            }
        }
        // willing to pay additionally up to the surplus benefit compared to 2nd best option
        min_cost_visit.value += min_cost_visit.price - min_cost;  // + 2nd best min cost (already stored)
        // update cost estimate of current visit to the min cost of adjacent visits
        auto& bid = bids.find(visit.price)->second;
        bid.cost_estimate = min_cost;
        bid.cost_nonce = _cost_nonce;
        // truncate rest of path and proceed to previous visit if current visit is a dead end
        if (!min_cost_visit.node) {
            path.resize(visit_index + 1);
            continue;
        }
        // if next visit in path is not the min cost visit found
        if (visit_index + 1 == static_cast<int>(path.size()) || path[visit_index + 1].node != min_cost_visit.node ||
                path[visit_index + 1].price != min_cost_visit.price) {
            // truncate path upto current visit and append min cost visit to path
            path.resize(visit_index + 1);
            path.push_back(std::move(min_cost_visit));
        }
    }
    return path.size() > original_path_size ? PATH_EXTENDED : PATH_CONTRACTED;
}

bool PathSearch::detectCycle(const Auction::Bid& bid, const Path& path) {
    ++_cycle_nonce;
    // set all previous visits in path as visited
    for (auto& visit : path) {
        assert(Graph::validateNode(visit.node));
        auto& visit_bid = visit.node->auction.getBids().find(visit.price)->second;
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

PathSearch::Error PathSearch::finalizePrice(Path& path) const {
    if (path.empty()) {
        return PATH_EMPTY;
    }
    for (auto& visit : path) {
        if (!_graph.containsNode(visit.node)) {
            return PATH_NODE_NOT_FOUND;
        }
        if (visit.node->state >= Graph::Node::DISABLED) {
            return PATH_NODE_DISABLED;
        }
        auto& bids = visit.node->auction.getBids();
        auto bid = bids.find(visit.price);
        if (bid == bids.end()) {
            return PATH_BID_NOT_FOUND;
        }
        if (bid->second.bidder == _config.agent_id) {
            return PATH_BID_OVER_SELF;
        }
        // find next highest bid that is not from self
        while (++bid != bids.end() && bid->second.bidder == _config.agent_id)
            ;
        // use value as price if there is no higher bid, otherwise bid inbetween the slot
        visit.price = bid == bids.end() ? visit.value : (visit.price + bid->first) * 0.5f;
        visit.value = visit.price;
    }
    if (path.back().node->state == Graph::Node::NO_PARKING) {
        return PATH_PARKING_VIOLATION;
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
