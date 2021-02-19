#include <decentralized_path_auction/graph.hpp>

namespace decentralized_path_auction {

bool Graph::insertNode(NodePtr node) {
    if (!validateNode(node)) {
        return false;
    }
    // reject nodes with duplicate positions
    if (findNode(node->position)) {
        return false;
    }
    // insert to rtree
    RTreeNode rt_node{node->position, nullptr};
    rt_node.second = std::move(node);
    _nodes.insert(rt_node);
    return true;
}

bool Graph::removeNode(NodePtr node) {
    if (!node) {
        return false;
    }
    // clear edges to break shared_ptr cyclic references
    node->edges.clear();
    node->state = Node::DELETED;
    // remove from rtree
    RTreeNode rt_node{node->position, nullptr};
    rt_node.second = std::move(node);
    return _nodes.remove(rt_node);
}

void Graph::clearNodes() {
    for (auto& rt_node : _nodes) {
        // clear edges to break shared_ptr cyclic references
        rt_node.second->edges.clear();
        rt_node.second->state = Node::DELETED;
    }
    _nodes.clear();
}

Graph::RTree Graph::detachNodes() {
    RTree detached = std::move(_nodes);
    _nodes.clear();
    return detached;
}

NodePtr Graph::findNearestNode(Point2D position, Node::State criterion) const {
    return query(bg::index::nearest(position, 1) && bg::index::satisfies([criterion](const RTreeNode& rt_node) {
        return rt_node.second->state <= criterion;
    }));
}

NodePtr Graph::findAnyNode(Node::State criterion) const {
    return query(
            bg::index::satisfies([criterion](const RTreeNode& rt_node) { return rt_node.second->state <= criterion; }));
}

}  // namespace decentralized_path_auction
