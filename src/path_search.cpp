#include <decentralized_path_auction/path_search.hpp>

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
    // reset start node
    if (!node_valid(start_node)) {
        return INVALID_START_NODE;
    }
    _path.stops.clear();
    _path.stops.push_back(
            Path::Stop{*start_node->auction.getBids().begin(), std::move(start_node)});
    // reset goal nodes
    _goal_nodes = Graph();
    for (auto& goal_node : goal_nodes) {
        if (!_goal_nodes.insertNode(std::move(goal_node))) {
            return INVALID_GOAL_NODE;
        }
    }
    ++_search_id;
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
                auto& [price, bidder] = bid;
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
                // update rank cost if rank changed
                if (bidder.user_data->rank != --rank) {
                    bidder.user_data->rank = rank;
                    assert(_config.rank_cost && "rank_cost function is required");
                    float rank_cost = _config.rank_cost(rank);
                }
                // remove rank cross-overs between adjacent nodes to prevent collisions
            }
        }
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
