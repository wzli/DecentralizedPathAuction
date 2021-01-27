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
    ASSERT_EQ(pathway.back(), graph.findNearestNode({100, 13}, Graph::Node::ENABLED));
    ASSERT_EQ(pathway.front(), graph.findNearestNode({-100, -13}, Graph::Node::ENABLED));
    ASSERT_EQ(pathway[5], graph.findNearestNode({0.51, 5.1}, Graph::Node::ENABLED));
    // test remove and contains
    ASSERT_TRUE(graph.containsNode(pathway[5]));
    graph.removeNode(pathway[5]);
    ASSERT_FALSE(graph.containsNode(pathway[5]));
    ASSERT_EQ(graph.getNodes().size(), 10);
    ASSERT_EQ(pathway[5]->state, Graph::Node::DELETED);
    ASSERT_EQ(pathway[6], graph.findNearestNode({0.51, 5.1}, Graph::Node::ENABLED));
    // test find
    ASSERT_TRUE(graph.findNode({0, 0}));
    ASSERT_FALSE(graph.findNode({-1, -1}));
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

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
