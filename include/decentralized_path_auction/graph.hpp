#pragma once

#include <decentralized_path_auction/auction.hpp>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/index/rtree.hpp>

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
        enum State { DEFAULT, NO_PARKING, DELETED } state;

        // Constructor
        Node(Point2D position_, float base_toll_ = 0, State state_ = DEFAULT)
                : auction(base_toll_)
                , position(position_)
                , state(state_) {}
    };

    using NodePtr = std::shared_ptr<Node>;
    using Nodes = std::vector<NodePtr>;
    using RTreeNode = std::pair<Point2D, NodePtr>;
    using RTree = bg::index::rtree<RTreeNode, bg::index::rstar<16>>;

    bool insertNode(NodePtr node);
    bool removeNode(NodePtr node);
    NodePtr queryNearestNode(Point2D position) const;
    const RTree& getNodes() const { return _nodes; }

private:
    RTree _nodes;
};

}  // namespace decentralized_path_auction
