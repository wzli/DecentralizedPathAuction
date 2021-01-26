#include <decentralized_path_auction/path_search.hpp>

#include <algorithm>
#include <cassert>

namespace decentralized_path_auction {

PathSearch::Error PathSearch::resetSearch(Graph::NodePtr src_node, Graph::Nodes dst_nodes) {
    if (_graph.getNodes().empty()) {
        return EMPTY_GRAPH;
    }
    if (!_config.validate()) {
        return INVALID_CONFIG;
    }
    // reset and check start node
    if (!_graph.containsNode(src_node) && src_node->state == Graph::Node::DISABLED) {
        return INVALID_START_NODE;
    }
    _src_node = src_node;
    // add start visit to path
    _path.clear();
    auto& highest_bid = src_node->auction.getHighestBid(_config.agent_id);
    _path.push_back(Visit{std::move(src_node), highest_bid.price, 0, 0});
    // don't modify destination if input is empty
    if (dst_nodes.empty()) {
        return SUCCESS;
    }
    // only reset cost estimates when new destinations are added
    _search_id += std::any_of(dst_nodes.begin(), dst_nodes.end(),
            [this](const Graph::NodePtr& node) { return !_dst_nodes.containsNode(node); });
    // reset destination nodes
    _dst_nodes.clearNodes(false);
    for (auto& goal_node : dst_nodes) {
        // verify destination node is in graph
        if (!_graph.containsNode(goal_node) || goal_node->state != Graph::Node::ENABLED) {
            return INVALID_GOAL_NODE;
        }
        _dst_nodes.insertNode(std::move(goal_node));
    }
    return SUCCESS;
}

PathSearch::Error PathSearch::iterateSearch() {
    if (_dst_nodes.getNodes().empty()) {
        return EMPTY_GOAL_NODES;
    }
    size_t original_path_size = _path.size();
    // truncate visits in path that are invalid (node got deleted/disabled or bid got removed)
    _path.erase(std::find_if(_path.begin(), _path.end(),
                        [](const Visit& visit) {
                            return !Graph::validateNode(visit.node) || visit.node->state == Graph::Node::DISABLED ||
                                   !visit.node->auction.getBids().count(visit.price);
                        }),
            _path.end());
    // reset src visit if it got invalidated or outbid
    if (_path.empty() || _path.front().price != _path.front().node->auction.getHighestBid().price) {
        if (auto reset_error = resetSearch(_src_node)) {
            return reset_error;
        }
    }
    // termination condition
    if (_dst_nodes.containsNode(_path.back().node)) {
        return SUCCESS;
    }
    // iterate in reverse order through each visit in path
    for (int visit_index = _path.size() - 1; visit_index >= 0; --visit_index) {
        // use index to iterate since path will be modified at the end of each loop
        auto& visit = _path[visit_index];
        auto& bids = visit.node->auction.getBids();
        // store the min cost adjacent visit found
        float min_cost = std::numeric_limits<float>::max();
        Visit min_cost_visit{nullptr, 0, min_cost, 0};
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
            float wait_duration = 0;
            // iterate in reverse order (highest to lowest) through each bid in the auction of the adjacent node
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
                if (checkCollision(bid, visit_index)) {
                    continue;
                }
                // arrival_time factors in how long you have to wait
                float arrival_time = std::max(wait_duration, earliest_arrival_time);
                // time cost is proportinal to difference in expected arrival time from current node to adjacent node
                float time_cost = (arrival_time - visit.time) * _config.time_exchange_rate;
                assert(time_cost >= 0);
                // total cost of visit aggregates time cost, price of bid, and estimated cost from adj node to dst
                float cost_estimate = time_cost + bid.price + getCostEstimate(adj_node, bid);
                // keep track of lowest cost visit found
                if (cost_estimate < min_cost) {
                    min_cost = cost_estimate;
                    min_cost_visit.node = std::move(adj_node);
                    min_cost_visit.price = bid.price;
                    min_cost_visit.time = arrival_time;
                } else {
                    // store second best cost in the value field
                    min_cost_visit.value = std::min(cost_estimate, min_cost_visit.value);
                }
            }
        }
        // willing to pay additionally up to the surplus benefit compared to 2nd best option
        min_cost_visit.value += min_cost_visit.price - min_cost;  // + 2nd best min cost (already stored)
        // update cost estimate of current visit to the min cost of adjacent visits
        auto& [_, bid] = *bids.find(visit.price);
        bid.cost_estimate = min_cost;
        bid.search_id = _search_id;
        // truncate rest of path and proceed to previous visit if current visit is a dead end
        if (!min_cost_visit.node) {
            _path.resize(visit_index + 1);
            continue;
        }
        // if next visit in path is not the min cost visit found
        if (visit_index + 1 == static_cast<int>(_path.size()) || _path[visit_index + 1].node != min_cost_visit.node ||
                _path[visit_index + 1].price != min_cost_visit.price) {
            // truncate path upto current visit and append min cost visit to path
            _path.resize(visit_index + 1);
            _path.push_back(std::move(min_cost_visit));
        }
    }
    return _path.size() > original_path_size ? EXTENDED_PATH : CONTRACTED_PATH;
}

float PathSearch::getCostEstimate(const Graph::NodePtr& node, const Auction::Bid& bid) const {
    // reset cost estimate when search id changes
    if (bid.search_id != _search_id) {
        bid.search_id = _search_id;
        assert(node && "node should exist");
        auto nearest_goal = _dst_nodes.findNearestNode(node->position, Graph::Node::ENABLED);
        bid.cost_estimate = _config.travel_time(nullptr, node, nearest_goal) * _config.time_exchange_rate;
    };
    return bid.cost_estimate;
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
