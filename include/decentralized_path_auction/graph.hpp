#pragma once

#include <memory>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <decentralized_path_auction/auction.hpp>

namespace decentralized_path_auction {

namespace bg = boost::geometry;

#ifndef DPA_NDIM
#define DPA_NDIM 3
#endif

using Point = bg::model::point<float, DPA_NDIM, bg::cs::cartesian>;

struct Node {
    const Point position;
    enum State { DEFAULT, NO_PARKING, NO_STOPPING, DISABLED, DELETED } state = DEFAULT;
    std::vector<std::shared_ptr<Node>> edges = {};
    Auction auction = {};
    void* custom_data = nullptr;

    static bool validate(const std::shared_ptr<Node>& node) { return node && node->state != Node::DELETED; }
};

using NodePtr = std::shared_ptr<Node>;
using Nodes = std::vector<NodePtr>;

struct Visit {
    NodePtr node;
    float price = 0;
    float duration = 0;
    float base_price = 0;
    float cost_estimate = 0;
    float time_estimate = 0;
};

using Path = std::vector<Visit>;

class NodeRTree {
public:
    using RTreeNode = std::pair<Point, NodePtr>;
    using RTree = bg::index::rtree<RTreeNode, bg::index::rstar<16>>;

    bool insertNode(NodePtr node);

    // virtual to allow derived class to manage node ownership
    virtual bool removeNode(NodePtr node);
    virtual void clearNodes() { _nodes.clear(); }

    template <class Predicate>
    NodePtr query(const Predicate& predicate) const {
        auto q = _nodes.qbegin(predicate);
        return q == _nodes.qend() ? nullptr : std::move(q->second);
    }

    NodePtr findNode(Point position) const { return query(bg::index::contains(position)); }
    NodePtr findNearestNode(Point position, Node::State criterion) const;
    NodePtr findAnyNode(Node::State criterion) const;

    const RTree& getNodes() const { return _nodes; }
    bool containsNode(const NodePtr& node) const { return Node::validate(node) && (node == findNode(node->position)); }

protected:
    RTree _nodes;
};

class Graph : public NodeRTree {
public:
    // non-copyable but movable (to force ownership of nodes to a single graph instance)
    ~Graph() { clearNodes(); }
    Graph& operator=(Graph&& rhs) { return clearNodes(), _nodes.swap(rhs._nodes), *this; }

    void clearNodes() override;
    bool removeNode(NodePtr node) override;
    bool removeNode(Point position) { return removeNode(findNode(position)); }

    using NodeRTree::insertNode;
    template <class... Args>
    NodePtr insertNode(Point position, Args&&... args) {
        auto node = NodePtr(new Node{position, std::forward<Args>(args)...});
        return insertNode(node) ? std::move(node) : nullptr;
    }
};

}  // namespace decentralized_path_auction
