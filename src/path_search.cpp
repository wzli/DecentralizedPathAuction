#include <decentralized_path_auction/path_search.hpp>

#include <algorithm>
#include <cassert>

namespace decentralized_path_auction {

PathSearch::Error PathSearch::resetSearch(Graph::NodePtr start_node, Graph::Nodes goal_nodes) {
    if (_graph.getNodes().empty()) {
        return EMPTY_GRAPH;
    }
    if (!_config.validate()) {
        return INVALID_CONFIG;
    }
    // reset goal nodes
    _goal_nodes.clearNodes(false);
    for (auto& goal_node : goal_nodes) {
        // verify goal node is in graph
        if (!_graph.containsNode(goal_node) || goal_node->state != Graph::Node::ENABLED) {
            return INVALID_GOAL_NODE;
        }
        _goal_nodes.insertNode(std::move(goal_node));
    }
    // reset and check start node
    if (!_graph.containsNode(start_node)) {
        return INVALID_START_NODE;
    }
    // initialize start node data
    auto& bids = start_node->auction.getBids();
    auto highest_bid = bids.rbegin();
    while (highest_bid != bids.rend() && highest_bid->second.bidder == _config.agent_id) {
        ++highest_bid;
    }
    assert(highest_bid != bids.rend() && "expect highest bid to exist");
    // increment search id on every reset
    highest_bid->second.search_id = ++_search_id;
    highest_bid->second.cost_estimate = _goal_nodes.findNearestDistance(start_node->position, Graph::Node::ENABLED);
    // add start visit to path
    _path.clear();
    _path.push_back(Visit{std::move(start_node), highest_bid->second.price, 0});
    return SUCCESS;
}

PathSearch::Error PathSearch::iterateSearch() {
    if (_goal_nodes.getNodes().empty()) {
        return EMPTY_GOAL_NODES;
    }
    assert(!_path.empty() && "expect first visit to be at start node");
    // iterate in reverse order through each visit in path
    for (int visit_index = _path.size() - 1; visit_index >= 0; --visit_index) {
        // use index to iterate since path will be modified at the end of each loop
        auto& visit = _path[visit_index];
        // check if node in path got deleted
        if (!Graph::validateNode(visit.node)) {
            // exit routine if start node doesn't exist
            if (visit_index == 0) {
                return INVALID_START_NODE;
            }
            // otherwise, truncate from end of path up to the deleted node
            _path.resize(visit_index);
            continue;
        }
        auto& bids = visit.node->auction.getBids();
        auto found_bid = bids.find(visit.price);
        // if current bid doesn't exist, truncate from end of path up to to current visit
        if (found_bid == bids.end()) {
            _path.resize(visit_index);
            continue;
        }
        // store the best adjacent visit found
        float min_cost = std::numeric_limits<float>::max();
        Visit best_visit{nullptr, 0, 0};
        // remove edges to deleted nodes
        auto& edges = visit.node->edges;
        edges.erase(std::remove_if(edges.begin(), edges.end(), std::not_fn(Graph::validateNode)), edges.end());
        // loop over each adjacent node
        for (auto& adj_node : edges) {
            // skip disabled nodes
            if (adj_node->state == Graph::Node::DISABLED) {
                continue;
            }
            // calculate the expected time to arrive at the adjacent node (without wait)
            const auto& prev_node = visit_index == 0 ? nullptr : _path[visit_index - 1].node;
            float earliest_arrival_time = visit.time + _config.travel_time(prev_node, visit.node, adj_node);
            assert(earliest_arrival_time >= 0);
            // iterate in reverse order (highest to lowest) through each bid in the auction of the adjacent node
            float wait_duration = 0;
            auto& adj_bids = adj_node->auction.getBids();
            for (auto adj_bid = adj_bids.rbegin(); adj_bid != adj_bids.rend(); ++adj_bid) {
                auto& [_, bid] = *adj_bid;
                // skip bids that belong to this agent
                if (bid.bidder == _config.agent_id) {
                    continue;
                }
                // wait duration atleast as long as the next total duration of next highest bid in the auction
                if (adj_bid != adj_bids.rbegin()) {
                    wait_duration = std::max(wait_duration, std::prev(adj_bid)->second.totalDuration());
                }
                // skip the bid if it causes collision
                if (visit.node->auction.checkCollision(visit.price, bid.price, adj_bids, _config.agent_id)) {
                    continue;
                }
                // reset cost estimate when search id changes
                if (bid.search_id != _search_id) {
                    bid.search_id = _search_id;
                    bid.cost_estimate = _goal_nodes.findNearestDistance(adj_node->position, Graph::Node::ENABLED);
                };
                // arrival_time factors in how long you have to wait
                float arrival_time = std::max(wait_duration, earliest_arrival_time);
                // time cost is proportinal to difference in expected arrival time from current node to adjacent node
                float time_cost = (arrival_time - visit.time) * _config.time_exchange_rate;
                assert(time_cost >= 0);
                // total cost of visit aggregates time cost, price of bid, and estimated cost from adj node to goal
                float cost_estimate = time_cost + bid.price + bid.cost_estimate;
                // keep track of lowest cost visit found
                if (cost_estimate < min_cost) {
                    min_cost = cost_estimate;
                    best_visit = {adj_node, bid.price, arrival_time};
                }
            }
        }
        // update min cost of the bid in current visit to the cost of the best next visit found
        found_bid->second.cost_estimate = min_cost;
        // proceed to previous visit if current visit is a dead end
        if (!best_visit.node) {
            _path.resize(visit_index + 1);
            continue;
        }
        // if next visit in path is not the best visit found
        if (visit_index + 1 == static_cast<int>(_path.size()) || _path[visit_index + 1].node != best_visit.node ||
                _path[visit_index + 1].price != best_visit.price) {
            // truncate path upto current visit and append best visit to path
            _path.resize(visit_index + 1);
            _path.push_back(std::move(best_visit));
        }
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
