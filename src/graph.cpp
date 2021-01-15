#include <decentralized_path_auction/graph.hpp>

namespace decentralized_path_auction {

Graph::NodePtr Graph::insertNode(Point2D position) {
    auto new_node = std::make_shared<Node>(Node{position});
    _nodes.insert(RTreeNode(position, new_node));
    return new_node;
}

Graph::NodePtr Graph::queryNearestNode(Point2D position) const {
    auto query = _nodes.qbegin(bg::index::nearest(position, 1));
    return query == _nodes.qend() ? nullptr : query->second;
}

bool Graph::removeNode(const Graph::NodePtr& node) {
    if (!node) {
        return false;
    }
    node->state = Node::DELETED;
    return _nodes.remove(RTreeNode(node->position, std::move(node)));
}

}  // namespace decentralized_path_auction
