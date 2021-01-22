#include <decentralized_path_auction/path_search.hpp>

#include <algorithm>
#include <cassert>

namespace decentralized_path_auction {

static constexpr auto node_valid = [](const Graph::NodePtr& node) {
    return !node || node->state == Graph::Node::DELETED;
};

static void erase_invalid_nodes(Graph::Nodes& nodes) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), node_valid), nodes.end());
}

static float distance_to_nodes(const Graph& nodes, Point2D position) {
    auto nearest = nodes.queryNearestNode(position);
    return nearest ? bg::distance(nearest->position, position) : 0;
}

PathSearch::Error PathSearch::resetSearch(Graph::NodePtr start_node, Graph::Nodes goal_nodes) {
    // reset goal nodes
    _goal_nodes = Graph();
    for (auto& goal_node : goal_nodes) {
        if (!_goal_nodes.insertNode(std::move(goal_node))) {
            return INVALID_GOAL_NODE;
        }
    }
    // initialize start node data
    if (!node_valid(start_node)) {
        return INVALID_START_NODE;
    }
    auto& [_, bid] = *start_node->auction.getBids().rbegin();
    bid.search_id = ++_search_id;
    bid.cost_estimate = distance_to_nodes(_goal_nodes, start_node->position);
    // add start node to path
    _path.stops.clear();
    _path.stops.push_back(Path::Stop{std::move(start_node), bid.price, 0});
    return SUCCESS;
}

PathSearch::Error PathSearch::iterateSearch() {
    if (_goal_nodes.getNodes().empty()) {
        return EMPTY_GOAL_NODES;
    }
    assert(!_path.stops.empty() && "expect first stop to be start node");
    // iterate through each stop from the end
    for (int stop_index = _path.stops.size() - 1; stop_index >= 0; --stop_index) {
        auto& stop = _path.stops[stop_index];
        if (!node_valid(stop.node)) {
            if (stop_index == 0) {
                return INVALID_START_NODE;
            };
            // TODO: handle when node in path is deleted
        }
        auto& auction = stop.node->auction;
        auto& bids = auction.getBids();
        auto found_bid = bids.find(stop.price);
        if (found_bid == bids.end()) {
            // TODO: handle when reference bid doesn't exist
        }
        // remove edges to deleted nodes
        erase_invalid_nodes(stop.node->edges);
        // store the best branch found
        float min_cost = std::numeric_limits<float>::max();
        Path::Stop best_stop{nullptr, 0, 0};
        // loop over each adjacent node
        for (auto& adj_node : stop.node->edges) {
            assert(node_valid(adj_node) && "adj node should exist");
            auto prev_node = stop_index == 0 ? nullptr : (&stop - 1)->node;
            float base_time_estimate =
                    stop.time_estimate +
                    _config.travel_time(std::move(prev_node), stop.node, adj_node);
            // loop over each bid in auction of the adjacent node
            auto& adj_bids = adj_node->auction.getBids();
            float bid_time = 0;
            for (auto adj_bid = adj_bids.rbegin(); adj_bid != adj_bids.rend(); ++adj_bid) {
                auto& [_, bid] = *adj_bid;
                bid_time = std::max(bid_time, bid.sumTravelTime());
                // skip if the bid causes collision
                if (auction.checkCollision(stop.price, bid.price, adj_bids, _config.agent_id)) {
                    continue;
                }
                // reset cost estimate when search id changes
                if (bid.search_id != _search_id) {
                    bid.search_id = _search_id;
                    bid.cost_estimate = distance_to_nodes(_goal_nodes, adj_node->position);
                };
                float time_estimate = std::max(bid_time, base_time_estimate);
                float time_cost = time_estimate - stop.time_estimate;
                float cost_estimate = bid.cost_estimate + bid.price + time_cost;
                if (cost_estimate < min_cost) {
                    min_cost = cost_estimate;
                    best_stop = {adj_node, bid.price, time_estimate};
                }
            }
        }
        assert(best_stop.node);
        stop.time_estimate = best_stop.time_estimate;
        found_bid->second.cost_estimate = min_cost;

        if (stop_index + 1 == _path.stops.size()) {
            _path.stops.push_back(std::move(best_stop));
        } else if ((&stop + 1)->node != best_stop.node || (&stop + 1)->price != best_stop.price) {
            _path.stops.resize(stop_index + 1);
            _path.stops.push_back(std::move(best_stop));
        }
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
