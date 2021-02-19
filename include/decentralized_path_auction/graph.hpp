#pragma once

#include <decentralized_path_auction/auction.hpp>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <memory>

namespace decentralized_path_auction {

namespace bg = boost::geometry;
using Point2D = bg::model::d2::point_xy<float>;

class Graph {
public:
    struct Node {
        const Point2D position;
        enum State { DEFAULT, NO_PARKING, NO_STOPPING, DISABLED, DELETED } state = DEFAULT;
        std::vector<std::shared_ptr<Node>> edges = {};
        Auction auction = {};
    };

    using NodePtr = std::shared_ptr<Node>;
    using Nodes = std::vector<NodePtr>;
    using RTreeNode = std::pair<Point2D, NodePtr>;
    using RTree = bg::index::rtree<RTreeNode, bg::index::rstar<16>>;

    // non-copyable but movable (to force ownership of nodes to a single graph instance)
    ~Graph() { clearNodes(); }
    Graph& operator=(Graph&& rhs) { return clearNodes(), _nodes.swap(rhs._nodes), *this; }

    bool insertNode(NodePtr node);
    bool removeNode(NodePtr node);

    // position based insert and remove
    template <class... Args>
    bool insertNode(Point2D position, Args&&... args) {
        return insertNode(NodePtr(new Node{position, std::forward<Args>(args)...}));
    }
    bool removeNode(Point2D position) { return removeNode(findNode(position)); }

    // nodes are deleted when cleared but not when detached
    void clearNodes();
    // WARNING: must manually clear edge links from detached nodes to prevent cyclic shared_ptr memory leak
    RTree detachNodes();

    template <class Predicate>
    NodePtr query(const Predicate& predicate) const {
        auto q = _nodes.qbegin(predicate);
        return q == _nodes.qend() ? nullptr : std::move(q->second);
    }

    NodePtr findNode(Point2D position) const { return query(bg::index::contains(position)); }
    NodePtr findNearestNode(Point2D position, Node::State criterion) const;
    NodePtr findAnyNode(Node::State criterion) const;

    const RTree& getNodes() const { return _nodes; }
    bool containsNode(const NodePtr& node) const { return validateNode(node) && (node == findNode(node->position)); }
    static bool validateNode(const NodePtr& node) { return node && node->state != Node::DELETED; }

private:
    RTree _nodes;
};

struct Visit {
    Graph::NodePtr node;
    float time = 0;
    float price = 0;
    float base_price = 0;
    float cost_estimate = 0;
};

using Path = std::vector<Visit>;

}  // namespace decentralized_path_auction
