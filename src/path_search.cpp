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

void PathSearch::buildCollisionBlacklist(const Auction& auction, float price) {
    _collision_blacklist_upper.clear();
    _collision_blacklist_lower.clear();
    for (auto& [bid_price, bidder] : auction.getBids()) {
        for (int dir = -1; dir < 1; dir += 2) {
            Auction::Bidder collision_bidder = {bidder.name, bidder.index + dir};
            if (bid_price > price) {
                _collision_blacklist_upper.push_back(std::move(collision_bidder));
            } else if (bid_price < price) {
                _collision_blacklist_lower.push_back(std::move(collision_bidder));
            }
        }
    }
    std::sort(_collision_blacklist_upper.begin(), _collision_blacklist_upper.end());
    std::sort(_collision_blacklist_lower.begin(), _collision_blacklist_lower.end());
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
    auto winning_bid = start_node->auction.getBids().rbegin();
    winning_bid->second.user_data = std::make_shared<Auction::UserData>(
            Auction::UserData{++_search_id, distance_to_nodes(_goal_nodes, start_node->position)});
    _path.stops.clear();
    _path.stops.push_back(Path::Stop{*winning_bid, std::move(start_node)});
    return SUCCESS;
}

PathSearch::Error PathSearch::iterateSearch() {
    if (_goal_nodes.getNodes().empty()) {
        return EMPTY_GOAL_NODES;
    }
    assert(!_path.stops.empty() && "expect first stop to be start node");
    // iterate through each stop from the end
    for (auto stop = _path.stops.rbegin(); stop != _path.stops.rend(); ++stop) {
        assert(node_valid(stop->node) && "node should exist");
        // TODO: handle when reference bid doesn't exist
        // build blacklist of bids on adjacent nodes that will cause collision
        buildCollisionBlacklist(
                stop->node->auction, stop->reference_bid.second.user_data->cost_estimate);
        // remove edges to deleted nodes
        erase_invalid_nodes(stop->node->edges);
        // loop over each adjacent node
        for (auto& adj_node : stop->node->edges) {
            assert(node_valid(adj_node) && "adj node should exist");
            float edge_cost = bg::distance(stop->node->position, adj_node->position);
            // add user provided traversal cost to edge cost
            if (_config.traversal_cost) {
                auto prev_node = stop + 1 == _path.stops.rend() ? nullptr : (stop + 1)->node;
                edge_cost += _config.traversal_cost(std::move(prev_node), stop->node, adj_node);
            }
            // loop over each bid in auction
            auto& bids = adj_node->auction.getBids();
            size_t rank = bids.size();
            for (auto& bid : bids) {
                auto& [bid_price, bidder] = bid;
                // skip if bid causes collision
                // create user data if it doesn't exist
                if (!bidder.user_data) {
                    bidder.user_data = std::make_shared<Auction::UserData>();
                }
                // reset cost estimate when search id changes
                if (bidder.user_data->search_id != _search_id) {
                    bidder.user_data->search_id = _search_id;
                    bidder.user_data->cost_estimate =
                            distance_to_nodes(_goal_nodes, adj_node->position);
                };
                // remove rank cross-overs between adjacent nodes to prevent collisions
                // TODO: lookup collision black list
            }
        }
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
