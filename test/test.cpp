#include <decentralized_path_auction/graph.hpp>
#include <decentralized_path_auction/path_search.hpp>
#include <decentralized_path_auction/path_sync.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Graph::Nodes& pathway, Point2D a, Point2D b, size_t n,
        Graph::Node::State state = Graph::Node::ENABLED) {
    ASSERT_GT(n, 1);
    auto pos = a;
    auto inc = b;
    bg::subtract_point(inc, a);
    bg::divide_value(inc, n - 1);
    pathway.clear();
    for (size_t i = 0; i < n; ++i, bg::add_point(pos, inc)) {
        auto node = std::make_shared<Graph::Node>(pos, state);
        ASSERT_TRUE(graph.insertNode(node));
        if (i > 0) {
            node->edges.push_back(pathway.back());
            pathway.back()->edges.push_back(node);
        }
        pathway.push_back(std::move(node));
    }
}

void print_graph(const Graph& graph) {
    auto rtree = graph.getNodes();
    for (auto qit = rtree.qbegin(bg::index::nearest(Point2D{0, 0}, rtree.size())); qit != rtree.qend(); ++qit) {
        const auto& [pos, node] = *qit;
        printf("(%.2f, %.2f):", pos.x(), pos.y());
        for (const auto& adj_node : node->edges) {
            printf(" (%.2f, %.2f, %u)", adj_node->position.x(), adj_node->position.y(), adj_node->state);
        }
        puts("");
    }
    printf("%lu nodes total\r\n", graph.getNodes().size());
}

TEST(graph, clear_or_destruct) {
    Graph::Nodes nodes;
    {
        Graph graph;
        make_pathway(graph, nodes, {0, 0}, {1, 10}, 11);
    }
    EXPECT_FALSE(nodes.empty());
    for (auto& node : nodes) {
        EXPECT_TRUE(node->edges.empty());
        EXPECT_EQ(node->state, Graph::Node::DELETED);
        EXPECT_EQ(node.use_count(), 1);
    }
}

TEST(graph, detach) {
    Graph graph;
    Graph::Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {1, 10}, 11);
    EXPECT_FALSE(nodes.empty());
    EXPECT_EQ(nodes.size(), graph.getNodes().size());
    EXPECT_EQ(nodes.size(), graph.detachNodes().size());
    EXPECT_TRUE(graph.getNodes().empty());
    for (auto& node : nodes) {
        EXPECT_FALSE(node->edges.empty());
        EXPECT_NE(node->state, Graph::Node::DELETED);
        EXPECT_NE(node.use_count(), 1);
    }
}

TEST(graph, move_assign) {
    Graph::Nodes nodes1, nodes2;
    Graph graph1, graph2;
    make_pathway(graph1, nodes1, {0, 0}, {1, 10}, 11);
    make_pathway(graph2, nodes2, {0, 0}, {1, 10}, 11);
    EXPECT_FALSE(nodes1.empty());
    EXPECT_FALSE(nodes2.empty());
    graph1 = std::move(graph2);
    for (auto& node : nodes1) {
        EXPECT_TRUE(node->edges.empty());
        EXPECT_EQ(node->state, Graph::Node::DELETED);
        EXPECT_EQ(node.use_count(), 1);
    }
    for (auto& node : nodes2) {
        EXPECT_FALSE(node->edges.empty());
        EXPECT_NE(node->state, Graph::Node::DELETED);
        EXPECT_NE(node.use_count(), 1);
    }
}

TEST(graph, insert) {
    Graph graph;
    auto node = std::make_shared<Graph::Node>(Point2D{0, 0});
    // valid insert
    ASSERT_TRUE(graph.insertNode(node));
    // can't insert duplicate positions
    ASSERT_FALSE(graph.insertNode(node));
    // cant insert deleted node
    auto deleted_node = std::make_shared<Graph::Node>(Point2D{1, 1}, Graph::Node::DELETED);
    ASSERT_FALSE(graph.insertNode(deleted_node));
    // null check
    ASSERT_FALSE(graph.insertNode(nullptr));
    // only first insert was valid
    ASSERT_EQ(graph.getNodes().size(), 1);
}

TEST(graph, remove) {
    Graph graph;
    // null check
    ASSERT_FALSE(graph.removeNode(nullptr));
    // reject delete non-existing node
    auto node = std::make_shared<Graph::Node>(Point2D{0, 0});
    ASSERT_FALSE(graph.removeNode(std::move(node)));
    // valid delete
    node = std::make_shared<Graph::Node>(Point2D{0, 0});
    node->edges.push_back(nullptr);
    ASSERT_TRUE(graph.insertNode(node));
    ASSERT_TRUE(graph.removeNode(node));
    // node should be in deleted state
    ASSERT_TRUE(node->edges.empty());
    ASSERT_EQ(node->state, Graph::Node::DELETED);
    ASSERT_EQ(node.use_count(), 1);
    // all nodes removed
    ASSERT_TRUE(graph.getNodes().empty());
}

TEST(graph, find) {
    Graph graph;
    Graph::Nodes pathway;
    make_pathway(graph, pathway, {0, 0}, {1, 10}, 11, Graph::Node::NO_PARKING);
    // test find
    EXPECT_TRUE(graph.findNode({0, 0}));
    EXPECT_FALSE(graph.findNode({-1, -1}));
    // test find nearest
    EXPECT_EQ(nullptr, graph.findNearestNode({100, 13}, Graph::Node::ENABLED));
    EXPECT_EQ(pathway.back(), graph.findNearestNode({100, 13}, Graph::Node::NO_PARKING));
    EXPECT_EQ(pathway.front(), graph.findNearestNode({-100, -13}, Graph::Node::NO_PARKING));
    EXPECT_EQ(pathway[5], graph.findNearestNode({0.51, 5.1}, Graph::Node::NO_PARKING));
    // test contains
    EXPECT_TRUE(graph.containsNode(pathway[5]));
    EXPECT_TRUE(graph.removeNode(pathway[5]));
    EXPECT_FALSE(graph.containsNode(pathway[5]));
    // test validate
    EXPECT_TRUE(Graph::validateNode(pathway[4]));
    EXPECT_FALSE(Graph::validateNode(pathway[5]));
    EXPECT_FALSE(Graph::validateNode(nullptr));
    // test find any
    EXPECT_TRUE(graph.findAnyNode(Graph::Node::NO_PARKING));
    EXPECT_FALSE(graph.findAnyNode(Graph::Node::ENABLED));
    graph.clearNodes();
    EXPECT_FALSE(graph.findAnyNode(Graph::Node::NO_PARKING));

    // print_graph(graph);
}

#if 0
TEST(auction, insert_remove_bids) {
    Auction auction(10);
    EXPECT_EQ(auction.getStartPrice(), 10);
    EXPECT_EQ(auction.getBids().size(), 1);
    EXPECT_EQ(auction.getBids().begin()->second.higher, nullptr);
    // reject empty bidder
    Auction::Bid* prev = nullptr;
    EXPECT_FALSE(auction.insertBid({"", 11}, prev));
    EXPECT_FALSE(prev);
    EXPECT_EQ(auction.getBids().size(), 1);
    // reject less than start price
    EXPECT_FALSE(auction.insertBid({"A", 5}, prev));
    // valid insert
    EXPECT_TRUE(auction.insertBid({"A", 11}, prev));
    EXPECT_EQ(auction.getHighestBid().price, 11);
    EXPECT_EQ(auction.getHighestBid("A").price, 10);
    EXPECT_EQ(auction.getBids().begin()->second.higher, &auction.getHighestBid());
    EXPECT_EQ(auction.getHighestBid().higher, nullptr);
    auto first = prev;
    EXPECT_EQ(auction.getBids().size(), 2);
    EXPECT_TRUE(prev);
    EXPECT_EQ(prev->bidder, "A");
    EXPECT_FALSE(prev->prev);
    EXPECT_FALSE(prev->next);
    // reject double insert
    EXPECT_FALSE(auction.insertBid({"A", 11}, prev));
    // reject bidder mismatch
    EXPECT_FALSE(auction.insertBid({"B", 12}, prev));
    // insert linked bids
    EXPECT_TRUE(auction.insertBid({"A", 12}, prev));
    EXPECT_TRUE(auction.insertBid({"A", 13}, prev));
    EXPECT_TRUE(auction.insertBid({"A", 14}, prev));
    EXPECT_EQ(auction.getHighestBid().price, 14);
    auto last = prev;
    // test links
    for (int i = 14; prev; prev = prev->prev, --i) {
        EXPECT_EQ(prev->price, i);
    }
    prev = first;
    for (int i = 11; prev; prev = prev->next, ++i) {
        EXPECT_EQ(prev->price, i);
    }
    EXPECT_EQ(auction.getBids().size(), 5);
    // reject false remove
    EXPECT_FALSE(auction.removeBid({"B", 1, 11}));
    // valid remove
    EXPECT_TRUE(auction.removeBid({"A", 12}));
    EXPECT_EQ(auction.getBids().size(), 4);
    EXPECT_EQ(first->price, 11);
    EXPECT_EQ(first->next->price, 13);
    EXPECT_TRUE(auction.removeBid({"A", 11}));
    prev = last;
    for (int i = 14; prev; prev = prev->prev, --i) {
        EXPECT_EQ(prev->price, i);
    }
    EXPECT_EQ(auction.getBids().size(), 3);
    // test higher links
    for (auto bid = auction.getBids().begin(); bid != auction.getBids().end(); ++bid) {
        auto higher = std::next(bid) == auction.getBids().end() ? nullptr : &std::next(bid)->second;
        EXPECT_EQ(bid->second.higher, higher);
    }
}

TEST(auction, collision_checks) {
    Graph graph;
    Graph::Nodes pathway;
    make_pathway(graph, pathway, {0, 0}, {1, 1}, 2);
    PathSearch::Config config{"A"};
    Auction::Bid* prev = nullptr;
    ASSERT_TRUE(pathway[0]->auction.insertBid({"B", 5}, prev));
    ASSERT_TRUE(pathway[1]->auction.insertBid({"B", 6}, prev));
    prev = nullptr;
    ASSERT_TRUE(pathway[1]->auction.insertBid({"A", 5}, prev));
    auto& bids = pathway[1]->auction.getBids();
    ASSERT_FALSE(pathway[0]->auction.checkCollision(3, 4, bids));
    ASSERT_FALSE(pathway[0]->auction.checkCollision(3, 5, bids));
    ASSERT_TRUE(pathway[0]->auction.checkCollision(3, 7, bids));
    ASSERT_TRUE(pathway[0]->auction.checkCollision(3, 8, bids));
    ASSERT_TRUE(pathway[0]->auction.checkCollision(6, 4, bids));
    ASSERT_TRUE(pathway[0]->auction.checkCollision(6, 5, bids));
    ASSERT_FALSE(pathway[0]->auction.checkCollision(6, 7, bids));
    ASSERT_FALSE(pathway[0]->auction.checkCollision(6, 8, bids));
}
#endif

// TODO:
// test passive path
// test evading into aile
// test cyclic dependencies check
// test back and forth dependencies
// test if destructors of graph, auction, path search work as intended

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
