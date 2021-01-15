#pragma once

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
        const Point2D position;
        std::vector<std::shared_ptr<Node>> edges = {};
        float base_toll = 0;
        enum State { DEFAULT, NO_PARKING, DELETED } state = DEFAULT;
    };

    using NodePtr = std::shared_ptr<Node>;
    using RTreeNode = std::pair<Point2D, NodePtr>;
    using RTree = bg::index::rtree<RTreeNode, bg::index::rstar<16>>;

    NodePtr insertNode(Point2D position);
    NodePtr queryNearestNode(Point2D position) const;
    bool removeNode(const NodePtr& node);
    const RTree& getNodes() const { return _nodes; }

private:
    RTree _nodes;
};

}  // namespace decentralized_path_auction
