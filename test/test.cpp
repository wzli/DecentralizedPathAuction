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

TEST(graph, destruct_or_clear_nodes) {
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

TEST(graph, insert_node) {
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

TEST(graph, remove_node) {
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

TEST(graph, detach_nodes) {
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

TEST(graph, find_node) {
    Graph graph;
    EXPECT_FALSE(graph.findNode({0, 0}));
    ASSERT_TRUE(graph.insertNode(std::make_shared<Graph::Node>(Point2D{0, 0})));
    EXPECT_TRUE(graph.findNode({0, 0}));
}

TEST(graph, find_nearest_node) {
    Graph graph;
    Graph::Nodes pathway;
    make_pathway(graph, pathway, {0, 0}, {1, 10}, 11, Graph::Node::NO_PARKING);
    EXPECT_EQ(nullptr, graph.findNearestNode({100, 13}, Graph::Node::ENABLED));
    EXPECT_EQ(pathway.back(), graph.findNearestNode({100, 13}, Graph::Node::NO_PARKING));
    EXPECT_EQ(pathway.front(), graph.findNearestNode({-100, -13}, Graph::Node::NO_PARKING));
    EXPECT_EQ(pathway[5], graph.findNearestNode({0.51, 5.1}, Graph::Node::NO_PARKING));
}

TEST(graph, find_any_node) {
    Graph graph;
    EXPECT_FALSE(graph.findAnyNode(Graph::Node::ENABLED));
    ASSERT_TRUE(graph.insertNode(std::make_shared<Graph::Node>(Point2D{0, 0}, Graph::Node::NO_PARKING)));
    EXPECT_TRUE(graph.findAnyNode(Graph::Node::NO_PARKING));
    EXPECT_FALSE(graph.findAnyNode(Graph::Node::ENABLED));
}

TEST(graph, contains_node) {
    Graph graph;
    EXPECT_FALSE(graph.containsNode(nullptr));
    auto node = std::make_shared<Graph::Node>(Point2D{0, 0});
    EXPECT_FALSE(graph.containsNode(node));
    EXPECT_TRUE(graph.insertNode(node));
    EXPECT_TRUE(graph.containsNode(node));
}

TEST(graph, validate_node) {
    auto node = std::make_shared<Graph::Node>(Point2D{0, 0});
    EXPECT_TRUE(Graph::validateNode(node));
    node->state = Graph::Node::DELETED;
    EXPECT_FALSE(Graph::validateNode(node));
    EXPECT_FALSE(Graph::validateNode(nullptr));
}

TEST(auction, constructor) {
    Auction auction(10);
    EXPECT_EQ(auction.getStartPrice(), 10);
    EXPECT_EQ(auction.getBids().size(), 1);
    auto& [start_price, start_bid] = *auction.getBids().begin();
    EXPECT_EQ(start_price, 10);
    EXPECT_EQ(start_bid.bidder, "");
    EXPECT_EQ(start_bid.prev, nullptr);
    EXPECT_EQ(start_bid.next, nullptr);
    EXPECT_EQ(start_bid.higher, nullptr);
}

TEST(auction, destructor) {}

TEST(auction, insert_bid) {
    float start_price = 10;
    Auction auction(start_price);
    auto& bids = auction.getBids();
    Auction::Bid* prev = nullptr;
    // trivial input checks
    EXPECT_EQ(auction.insertBid("", 0, 0, prev), Auction::BIDDER_EMPTY);
    EXPECT_EQ(auction.insertBid("A", start_price, 0, prev), Auction::PRICE_BELOW_START);
    EXPECT_EQ(auction.insertBid("A", start_price - 1, 0, prev), Auction::PRICE_BELOW_START);
    EXPECT_EQ(auction.insertBid("A", start_price + 1, -1, prev), Auction::DURATION_NEGATIVE);
    // add first bid
    EXPECT_EQ(auction.insertBid("A", start_price + 1, 0, prev), Auction::SUCCESS);
    EXPECT_EQ(bids.begin()->second.higher, prev);
    EXPECT_EQ(prev->prev, nullptr);
    EXPECT_EQ(prev->next, nullptr);
    EXPECT_EQ(prev->higher, nullptr);
    // more rejection checks
    EXPECT_EQ(auction.insertBid("A", start_price + 1, 0, prev), Auction::PRICE_ALREADY_EXIST);
    EXPECT_EQ(auction.insertBid("B", start_price + 2, 0, prev), Auction::BIDDER_MISMATCH);
    // add chained bid
    for (int i = 2; i < 10; ++i) {
        EXPECT_EQ(auction.insertBid("A", start_price + i, 0, prev), Auction::SUCCESS);
    }
    EXPECT_EQ(prev->higher, nullptr);
    EXPECT_EQ(prev->next, nullptr);
    prev = prev->prev;
    EXPECT_EQ(auction.insertBid("A", start_price + 8.5f, 0, prev), Auction::SUCCESS);
    int i = 0;
    for (auto bid = bids.begin()->second.higher; bid; bid = bid->next) {
        if (bid->prev) {
            EXPECT_EQ(bid->prev->next, bid);
        }
        if (bid->next) {
            EXPECT_EQ(bid->next->prev, bid);
        }
        if (bid->higher) {
            EXPECT_EQ(bid->higher->prev, bid);
        }
        ++i;
    }
    EXPECT_EQ(i, bids.size() - 1);
}

TEST(auction, remove_bid) {}

TEST(auction, get_higher_bid) {}

TEST(auction, get_highest_bid) {}

TEST(auction, detect_cycle) {}

TEST(auction, total_duration) {}

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
// test back and forth path wait dependencies
// test if destructors of graph, auction, path search work as intended

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
