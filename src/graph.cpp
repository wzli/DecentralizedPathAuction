#include <decentralized_path_auction/graph.hpp>

namespace decentralized_path_auction {

bool Graph::insertNode(Graph::NodePtr node) {
    if (!node || node->state == Node::DELETED) {
        return false;
    }
    // reject nodes with duplicate positions
    auto nearest_node = queryNearestNode(node->position);
    if (nearest_node && bg::equals(nearest_node->position, node->position)) {
        return false;
    }
    _nodes.insert(RTreeNode(node->position, std::move(node)));
    return true;
}

bool Graph::removeNode(Graph::NodePtr node) {
    if (!node) {
        return false;
    }
    node->state = Node::DELETED;
    return _nodes.remove(RTreeNode(node->position, std::move(node)));
}

Graph::NodePtr Graph::queryNearestNode(Point2D position) const {
    auto query = _nodes.qbegin(bg::index::nearest(position, 1));
    return query == _nodes.qend() ? nullptr : query->second;
}

}  // namespace decentralized_path_auction
