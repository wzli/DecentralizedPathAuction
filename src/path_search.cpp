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
    for (auto stop = _path.stops.rbegin(); stop != _path.stops.rend(); ++stop) {
        if (!node_valid(stop->node)) {
            if (stop + 1 == _path.stops.rend()) {
                return INVALID_START_NODE;
            };
            // TODO: handle when node in path is deleted
        }
        auto& auction = stop->node->auction;
        auto& bids = auction.getBids();
        auto found_bid = bids.find(stop->price);
        if (found_bid == bids.end()) {
            // TODO: handle when reference bid doesn't exist
        }
        // remove edges to deleted nodes
        erase_invalid_nodes(stop->node->edges);
        // loop over each adjacent node
        for (auto& adj_node : stop->node->edges) {
            assert(node_valid(adj_node) && "adj node should exist");
            auto prev_node = stop + 1 == _path.stops.rend() ? nullptr : (stop + 1)->node;
            float travel_time = _config.travel_time(std::move(prev_node), stop->node, adj_node);
            // loop over each bid in auction of the adjacent node
            auto& adj_bids = adj_node->auction.getBids();
            for (auto& [_, bid] : adj_bids) {
                // skip if the bid causes collision
                if (auction.checkCollision(stop->price, bid.price, adj_bids, _config.agent_id)) {
                    continue;
                }
                // reset cost estimate when search id changes
                if (bid.search_id != _search_id) {
                    bid.search_id = _search_id;
                    bid.cost_estimate = distance_to_nodes(_goal_nodes, adj_node->position);
                };
            }
        }
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
