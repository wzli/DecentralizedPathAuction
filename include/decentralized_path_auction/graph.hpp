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
        enum State { ENABLED, NO_PARKING, DISABLED, DELETED } state;

        // Constructor
        Node(Point2D position_, float base_toll_ = 0, State state_ = ENABLED)
                : auction(base_toll_)
                , position(position_)
                , state(state_) {}
    };

    using NodePtr = std::shared_ptr<Node>;
    using Nodes = std::vector<NodePtr>;
    using RTreeNode = std::pair<Point2D, NodePtr>;
    using RTree = bg::index::rtree<RTreeNode, bg::index::rstar<16>>;

    bool insertNode(NodePtr node);
    bool removeNode(NodePtr node, bool mark_delete = true);
    void clearNodes(bool mark_delete = true);

    NodePtr findNode(Point2D position) const;
    NodePtr findNearestNode(Point2D position, Node::State threshold = Node::ENABLED) const;
    float findNearestDistance(Point2D position, Node::State threshold = Node::ENABLED) const;

    const RTree& getNodes() const { return _nodes; }
    bool containsNode(const NodePtr& node) const { return validateNode(node) && (node == findNode(node->position)); }
    static bool validateNode(const NodePtr& node) { return node && node->state != Node::DELETED; }

private:
    RTree _nodes;
};

}  // namespace decentralized_path_auction
