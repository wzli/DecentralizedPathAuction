#pragma once

#include <decentralized_path_auction/auction.hpp>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <memory>
#include <vector>

namespace decentralized_path_auction {

namespace bg = boost::geometry;
using Point2D = bg::model::d2::point_xy<float>;

class Graph {
public:
    struct Node {
        Auction auction;
        std::vector<std::shared_ptr<Node>> edges;
        const Point2D position;
        enum State { ENABLED, NO_PARKING, NO_STOPPING, DISABLED, DELETED } state;

        // Constructor
        Node(Point2D position_, float start_price_ = 0, State state_ = ENABLED)
                : auction(start_price_)
                , position(position_)
                , state(state_) {}
    };

    using NodePtr = std::shared_ptr<Node>;
    using Nodes = std::vector<NodePtr>;
    using RTreeNode = std::pair<Point2D, NodePtr>;
    using RTree = bg::index::rtree<RTreeNode, bg::index::rstar<16>>;

    // non-copyable but movable (to force ownership of nodes to a single graph instance)
    ~Graph() { clearNodes(); }
    Graph& operator=(Graph&&) = default;

    bool insertNode(NodePtr node);
    bool removeNode(NodePtr node);
    // mark all nodes as deleted when cleared but not when detached
    void clearNodes();
    RTree detachNodes();

    template <class Predicate>
    NodePtr query(const Predicate& predicate) const {
        auto query = _nodes.qbegin(predicate);
        return query == _nodes.qend() ? nullptr : std::move(query->second);
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
    float price = std::numeric_limits<float>::max();
    float min_price = 0;
};

using Path = std::vector<Visit>;

}  // namespace decentralized_path_auction
