#include <decentralized_path_auction/graph.hpp>

namespace decentralized_path_auction {

Graph::~Graph() {
    for (auto& rt_node : _nodes) {
        // clear edges to break shared_ptr cyclic references
        rt_node.second->edges.clear();
        rt_node.second->state = Node::DELETED;
    }
}

bool Graph::insertNode(Graph::NodePtr node) {
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

bool Graph::removeNode(Graph::NodePtr node) {
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

Graph::RTree Graph::detachNodes() {
    RTree detached;
    _nodes.swap(detached);
    // use named return value optimization instead of move
    return detached;
}

Graph::NodePtr Graph::findNearestNode(Point2D position, Node::State threshold) const {
    return query(bg::index::nearest(position, 1) && bg::index::satisfies([threshold](const RTreeNode& rt_node) {
        return rt_node.second->state <= threshold;
    }));
}

Graph::NodePtr Graph::findAnyNode(Node::State threshold) const {
    return query(
            bg::index::satisfies([threshold](const RTreeNode& rt_node) { return rt_node.second->state <= threshold; }));
}

}  // namespace decentralized_path_auction
