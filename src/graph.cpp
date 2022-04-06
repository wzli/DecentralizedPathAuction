#include <decentralized_path_auction/graph.hpp>

namespace decentralized_path_auction {

bool NodeRTree::insertNode(NodePtr node) {
    if (!Node::validate(node)) {
        return false;
    }
    // reject nodes with duplicate positions
    if (findNode(node->position)) {
        return false;
    }
    // insert to rtree
    auto pos = node->position;
    _nodes.insert({pos, std::move(node)});
    return true;
}

bool NodeRTree::removeNode(NodePtr node) {
    if (!node) {
        return false;
    }
    // remove from rtree
    auto pos = node->position;
    return _nodes.remove({pos, std::move(node)});
}

NodePtr NodeRTree::findNearestNode(Point position, Node::State criterion) const {
    return query(bg::index::nearest(position, 1) && bg::index::satisfies([criterion](const RTreeNode& rt_node) {
        return rt_node.second->state <= criterion;
    }));
}

NodePtr NodeRTree::findAnyNode(Node::State criterion) const {
    return query(
            bg::index::satisfies([criterion](const RTreeNode& rt_node) { return rt_node.second->state <= criterion; }));
}

////////////////////////////////////////////////////////////////////////////////

bool Graph::removeNode(NodePtr node) {
    if (node) {
        node->edges.clear();
        node->state = Node::DELETED;
    }
    return NodeRTree::removeNode(std::move(node));
}

void Graph::clearNodes() {
    for (auto& rt_node : _nodes) {
        // clear edges to break shared_ptr cyclic references
        rt_node.second->edges.clear();
        rt_node.second->state = Node::DELETED;
    }
    NodeRTree::clearNodes();
}

}  // namespace decentralized_path_auction
