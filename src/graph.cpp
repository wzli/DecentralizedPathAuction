#include <decentralized_path_auction/graph.hpp>

namespace decentralized_path_auction {

bool Graph::insertNode(Graph::NodePtr node) {
    if (!validateNode(node)) {
        return false;
    }
    // reject nodes with duplicate positions
    if (findNode(node->position)) {
        return false;
    }
    _nodes.insert(RTreeNode(node->position, std::move(node)));
    return true;
}

bool Graph::removeNode(Graph::NodePtr node, bool mark_delete) {
    if (!node) {
        return false;
    }
    if (mark_delete) {
        // clear edges to break shared_ptr cyclic references
        node->edges.clear();
        node->state = Node::DELETED;
    }
    return _nodes.remove(RTreeNode(node->position, std::move(node)));
}

void Graph::clearNodes(bool mark_delete) {
    if (mark_delete) {
        for (auto& rt_node : _nodes) {
            rt_node.second->edges.clear();
            rt_node.second->state = Node::DELETED;
        }
    }
    _nodes.clear();
}

Graph::NodePtr Graph::findNode(Point2D position) const {
    auto query = _nodes.qbegin(bg::index::contains(position));
    return query == _nodes.qend() ? nullptr : std::move(query->second);
}

Graph::NodePtr Graph::findNearestNode(Point2D position, Node::State threshold) const {
    auto query = _nodes.qbegin(
            bg::index::nearest(position, 1) &&
            bg::index::satisfies([threshold](const RTreeNode& rt_node) { return rt_node.second->state <= threshold; }));
    return query == _nodes.qend() ? nullptr : std::move(query->second);
}

}  // namespace decentralized_path_auction
