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
    size_t original_path_size = _path.size();
    // truncate visits in path that are invalid
    for (auto visit = _path.begin(); visit != _path.end(); ++visit) {
        // visit is invalid if node got deleted or bid got removed
        if (!Graph::validateNode(visit->node) || !visit->node->auction.getBids().count(visit->price)) {
            _path.erase(visit, _path.end());
            break;
        }
    }
    if (_path.empty()) {
        return INVALID_START_NODE;
    }
    // iterate in reverse order through each visit in path
    for (int visit_index = _path.size() - 1; visit_index >= 0; --visit_index) {
        // use index to iterate since path will be modified at the end of each loop
        auto& visit = _path[visit_index];
        auto& bids = visit.node->auction.getBids();
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
            // TODO: only start from bids priced lower than previous bid on the same node in current path
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
                if (checkCollision(bid, visit_index)) {
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
        auto found_bid = bids.find(visit.price);
        assert(found_bid != bids.end());
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
    // termination condition
    if (_goal_nodes.containsNode(_path.back().node)) {
        return SUCCESS;
    }
    return _path.size() > original_path_size ? EXTENDED_PATH : CONTRACTED_PATH;
}

bool PathSearch::checkCollision(const Auction::Bid& bid, int visit_index) {
    ++_collision_id;
    for (int i = 0; i <= visit_index; ++i) {
        assert(Graph::validateNode(_path[i].node));
        auto found_bid = _path[i].node->auction.getBids().find(_path[i].price);
        assert(found_bid != _path[i].node->auction.getBids().end());
        found_bid->second.collision_id = _collision_id;
    }
    return Auction::checkCollision(&bid, _collision_id, _config.agent_id);
}

}  // namespace decentralized_path_auction
