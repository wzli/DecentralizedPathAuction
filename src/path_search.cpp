#include <decentralized_path_auction/path_search.hpp>

#include <algorithm>
#include <cassert>

namespace decentralized_path_auction {

static constexpr auto node_valid = [](const Graph::NodePtr& node) {
    return !node || node->state == Graph::Node::DELETED;
};

static constexpr auto bid_compare = [](const Auction::Bid& a, const Auction::Bid& b) {
    return a.index == b.index ? a.bidder < b.bidder : a.index < b.index;
};

static void erase_invalid_nodes(Graph::Nodes& nodes) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), node_valid), nodes.end());
}

static float distance_to_nodes(const Graph& nodes, Point2D position) {
    auto nearest = nodes.queryNearestNode(position);
    return nearest ? bg::distance(nearest->position, position) : 0;
}

void PathSearch::buildCollisionBids(const Auction::Bids& bids, float price) {
    _collision_bids_upper.clear();
    _collision_bids_lower.clear();
    for (auto bid = std::next(bids.begin()); bid != bids.end(); ++bid) {
        // filter out your own bids
        if (bid->first.bidder == _config.agent_id) {
            continue;
        }
        for (int dir = -1; dir <= 1; dir += 2) {
            (bid->first.price > price ? _collision_bids_upper : _collision_bids_lower)
                    .push_back(Auction::Bid{bid->first.bidder, bid->first.index + dir, 0});
        }
    }
    std::sort(_collision_bids_upper.begin(), _collision_bids_upper.end(), bid_compare);
    std::sort(_collision_bids_lower.begin(), _collision_bids_lower.end(), bid_compare);
}

bool PathSearch::checkCollisionBids(const Auction::Bids& bids, float price) {
    for (auto bid = std::next(bids.begin()); bid != bids.end(); ++bid) {
        // filter out your own bids
        if (bid->first.bidder == _config.agent_id) {
            continue;
        }
        const auto& collision_bids =
                bid->first.price > price ? _collision_bids_lower : _collision_bids_upper;
        if (std::binary_search(
                    collision_bids.begin(), collision_bids.end(), bid->first, bid_compare)) {
            return true;
        }
    }
    return false;
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
    auto& [bid, data] = *start_node->auction.getBids().rbegin();
    data = std::make_shared<Auction::UserData>(
            Auction::UserData{++_search_id, distance_to_nodes(_goal_nodes, start_node->position)});
    // add start node to path
    _path.stops.clear();
    _path.stops.push_back(Path::Stop{bid, std::move(start_node)});
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
        auto& bids = stop->node->auction.getBids();
        auto found_bid = bids.find(stop->bid);
        if (found_bid == bids.end()) {
            // TODO: handle when reference bid doesn't exist
        }
        // build blacklist of bids on adjacent nodes that will cause collision
        buildCollisionBids(bids, found_bid->second->cost_estimate);
        // remove edges to deleted nodes
        erase_invalid_nodes(stop->node->edges);
        // loop over each adjacent node
        for (auto& adj_node : stop->node->edges) {
            assert(node_valid(adj_node) && "adj node should exist");
            // edge cost is proportional to distance
            float edge_cost = bg::distance(stop->node->position, adj_node->position);
            // add user provided traversal cost to edge cost
            if (_config.traversal_cost) {
                auto prev_node = stop + 1 == _path.stops.rend() ? nullptr : (stop + 1)->node;
                edge_cost += _config.traversal_cost(std::move(prev_node), stop->node, adj_node);
            }
            // loop over each bid in auction of the adjacent node
            auto& adj_bids = adj_node->auction.getBids();
            size_t rank = adj_bids.size();
            for (auto& [bid, data] : adj_bids) {
                // skip if the bid causes collision
                if (checkCollisionBids(adj_bids, bid.price)) {
                    continue;
                }
                // create user data if it doesn't exist
                if (!data) {
                    data = std::make_shared<Auction::UserData>();
                }
                // reset cost estimate when search id changes
                if (data->search_id != _search_id) {
                    data->search_id = _search_id;
                    data->cost_estimate = distance_to_nodes(_goal_nodes, adj_node->position);
                };
                // remove rank cross-overs between adjacent nodes to prevent collisions
                // TODO: lookup collision black list
            }
        }
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
