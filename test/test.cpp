#include <decentralized_path_auction/graph.hpp>
#include <decentralized_path_auction/path_search.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Graph::Nodes& pathway, Point2D a, Point2D b, size_t n) {
    ASSERT_GT(n, 1);
    auto pos = a;
    auto inc = b;
    bg::subtract_point(inc, a);
    bg::divide_value(inc, n - 1);
    pathway.clear();
    for (size_t i = 0; i < n; ++i, bg::add_point(pos, inc)) {
        auto node = std::make_shared<Graph::Node>(pos);
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
    for (auto qit = rtree.qbegin(bg::index::nearest(Point2D{0, 0}, rtree.size()));
            qit != rtree.qend(); ++qit) {
        const auto& [pos, node] = *qit;
        printf("(%.2f, %.2f):", pos.x(), pos.y());
        for (const auto& adj_node : node->edges) {
            printf(" (%.2f, %.2f, %u)", adj_node->position.x(), adj_node->position.y(),
                    adj_node->state);
        }
        puts("");
    }
    printf("%lu nodes total\r\n", graph.getNodes().size());
}

TEST(graph, insert_nearest_remove) {
    Graph graph;
    // test insert
    Graph::Nodes pathway;
    make_pathway(graph, pathway, {0, 0}, {1, 10}, 11);
    // test duplicate position rejection
    auto duplicate_node = std::make_shared<Graph::Node>(Point2D{0, 0});
    ASSERT_FALSE(graph.insertNode(duplicate_node));
    ASSERT_EQ(pathway.size(), 11);
    ASSERT_EQ(pathway.size(), graph.getNodes().size());
    // test nearest queries
    ASSERT_EQ(pathway.back(), graph.queryNearestNode({100, 13}));
    ASSERT_EQ(pathway.front(), graph.queryNearestNode({-100, -13}));
    ASSERT_EQ(pathway[5], graph.queryNearestNode({0.51, 5.1}));
    // test remove
    graph.removeNode(pathway[5]);
    ASSERT_EQ(graph.getNodes().size(), 10);
    ASSERT_EQ(pathway[5]->state, Graph::Node::DELETED);
    ASSERT_EQ(pathway[6], graph.queryNearestNode({0.51, 5.1}));
    // print_graph(graph);
}

TEST(auction, insert_remove_bids) {
    Auction auction(10);
    EXPECT_EQ(auction.getStartPrice(), 10);
    EXPECT_EQ(auction.getBids().size(), 1);
    // reject less than start price
    EXPECT_FALSE(auction.insertBid({"A", 0, 5}));
    EXPECT_EQ(auction.getBids().size(), 1);
    // valid insert
    EXPECT_TRUE(auction.insertBid({"A", 0, 11}));
    EXPECT_EQ(auction.getBids().size(), 2);
    // reject double insert
    EXPECT_FALSE(auction.insertBid({"A", 0, 11}));
    EXPECT_FALSE(auction.insertBid({"B", 1, 11}));
    // reject false remove
    EXPECT_FALSE(auction.removeBid({"B", 1, 11}));
    // valid remove
    EXPECT_TRUE(auction.removeBid({"A", 0, 11}));
    EXPECT_EQ(auction.getBids().size(), 1);
}

namespace decentralized_path_auction {
struct TestClass {
    static void test_collision_checks() {
        Graph graph;
        Graph::Nodes pathway;
        make_pathway(graph, pathway, {0, 0}, {1, 1}, 2);
        PathSearch::Config config{"A"};
        PathSearch path_search(std::move(graph), std::move(config));
        ASSERT_TRUE(pathway[0]->auction.insertBid({"B", 5, 5}));
        ASSERT_TRUE(pathway[1]->auction.insertBid({"B", 6, 6}));
        path_search.buildCollisionBids(pathway[0]->auction.getBids(), 4);
        ASSERT_FALSE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 4));
        ASSERT_FALSE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 5));
        ASSERT_TRUE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 7));
        ASSERT_TRUE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 8));
        path_search.buildCollisionBids(pathway[0]->auction.getBids(), 6);
        ASSERT_TRUE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 4));
        ASSERT_TRUE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 5));
        ASSERT_FALSE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 7));
        ASSERT_FALSE(path_search.checkCollisionBids(pathway[1]->auction.getBids(), 8));
    }
};
}  // namespace decentralized_path_auction

TEST(path_search, collision_checks) {
    TestClass::test_collision_checks();
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
