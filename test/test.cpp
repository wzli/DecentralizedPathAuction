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
        Graph::NodePtr node(new Graph::Node{pos, state});
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

////////////////////////////////////////////////////////////////////////////////

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
    Graph::NodePtr node(new Graph::Node{{0, 0}});
    // valid insert
    ASSERT_TRUE(graph.insertNode(node));
    // can't insert duplicate positions
    ASSERT_FALSE(graph.insertNode(node));
    // cant insert deleted node
    ASSERT_FALSE(graph.insertNode({1, 1}, Graph::Node::DELETED));
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
    Graph::NodePtr node(new Graph::Node{{0, 0}});
    ASSERT_FALSE(graph.removeNode(std::move(node)));
    // valid delete
    node = Graph::NodePtr(new Graph::Node{{0, 0}});
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
    auto detached_nodes = graph.detachNodes();
    EXPECT_EQ(nodes.size(), detached_nodes.size());
    EXPECT_TRUE(graph.getNodes().empty());
    for (auto& node : nodes) {
        EXPECT_FALSE(node->edges.empty());
        EXPECT_NE(node->state, Graph::Node::DELETED);
        EXPECT_NE(node.use_count(), 1);
    }
    // manually clear cyclic shared pointers
    for (auto& rt_node : detached_nodes) {
        rt_node.second->edges.clear();
    }
}

TEST(graph, find_node) {
    Graph graph;
    EXPECT_FALSE(graph.findNode({0, 0}));
    ASSERT_TRUE(graph.insertNode(Point2D{0, 0}));
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
    ASSERT_TRUE(graph.insertNode({0, 0}, Graph::Node::NO_PARKING));
    EXPECT_TRUE(graph.findAnyNode(Graph::Node::NO_PARKING));
    EXPECT_FALSE(graph.findAnyNode(Graph::Node::ENABLED));
}

TEST(graph, contains_node) {
    Graph graph;
    EXPECT_FALSE(graph.containsNode(nullptr));
    Graph::NodePtr node(new Graph::Node{{0, 0}});
    EXPECT_FALSE(graph.containsNode(node));
    EXPECT_TRUE(graph.insertNode(node));
    EXPECT_TRUE(graph.containsNode(node));
}

TEST(graph, validate_node) {
    Graph::NodePtr node(new Graph::Node{{0, 0}});
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

////////////////////////////////////////////////////////////////////////////////

void check_auction_links(const Auction::Bids& bids) {
    EXPECT_EQ(bids.rbegin()->second.higher, nullptr);
    EXPECT_EQ(bids.rbegin()->second.next, nullptr);
    EXPECT_EQ(bids.begin()->second.higher, &std::next(bids.begin())->second);
    EXPECT_EQ(bids.begin()->second.next, nullptr);
    EXPECT_EQ(bids.begin()->second.prev, nullptr);
    int i = 0;
    for (auto bid = &std::next(bids.begin())->second; bid; bid = bid->next, ++i) {
        if (bid->prev) {
            EXPECT_EQ(bid->prev->next, bid);
        } else {
            EXPECT_EQ(bid, &std::next(bids.begin())->second);
        }
        if (bid->next) {
            EXPECT_EQ(bid->next->prev, bid);
        } else {
            EXPECT_EQ(bid, &bids.rbegin()->second);
        }
        if (bid->higher) {
            EXPECT_EQ(bid->higher->prev, bid);
        } else {
            EXPECT_EQ(bid, &bids.rbegin()->second);
        }
    }
    EXPECT_EQ(i, bids.size() - 1);
}

TEST(auction, destructor) {
    Auction auc1(0);
    {
        Auction a3(0);
        {
            Auction auc2(0);
            for (int i = 1; i <= 5; ++i) {
                Auction::Bid* prev = nullptr;
                EXPECT_EQ(auc1.insertBid("A", i, 0, prev), Auction::SUCCESS);
                EXPECT_EQ(auc2.insertBid("A", i, 0, prev), Auction::SUCCESS);
                EXPECT_EQ(a3.insertBid("A", i, 0, prev), Auction::SUCCESS);
            }
        }
        for (auto bid = std::next(auc1.getBids().begin()); bid != auc1.getBids().end(); ++bid) {
            EXPECT_EQ(bid->second.prev, nullptr);
            EXPECT_EQ(bid->second.next->next, nullptr);
            EXPECT_EQ(bid->second.next->prev, &bid->second);
        }
    }
    for (auto bid = std::next(auc1.getBids().begin()); bid != auc1.getBids().end(); ++bid) {
        EXPECT_EQ(bid->second.prev, nullptr);
        EXPECT_EQ(bid->second.next, nullptr);
    }
    EXPECT_EQ(auc1.getBids().size(), 6);
}

TEST(auction, insert_bid) {
    Auction auction(0);
    auto& bids = auction.getBids();
    Auction::Bid* prev = nullptr;
    // trivial input checks
    EXPECT_EQ(auction.insertBid("", 0, 0, prev), Auction::BIDDER_EMPTY);
    EXPECT_EQ(auction.insertBid("A", 0, 0, prev), Auction::PRICE_BELOW_START);
    EXPECT_EQ(auction.insertBid("A", -1, 0, prev), Auction::PRICE_BELOW_START);
    EXPECT_EQ(auction.insertBid("A", 1, -1, prev), Auction::DURATION_NEGATIVE);
    // add first bid
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev), Auction::SUCCESS);
    EXPECT_EQ(bids.begin()->second.higher, prev);
    EXPECT_EQ(prev->prev, nullptr);
    EXPECT_EQ(prev->next, nullptr);
    EXPECT_EQ(prev->higher, nullptr);
    // more rejection checks
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev), Auction::PRICE_ALREADY_EXIST);
    EXPECT_EQ(auction.insertBid("B", 2, 0, prev), Auction::BIDDER_MISMATCH);
    // add chained bid
    for (int i = 2; i < 10; ++i) {
        EXPECT_EQ(auction.insertBid("A", i, 0, prev), Auction::SUCCESS);
    }
    EXPECT_EQ(prev->higher, nullptr);
    EXPECT_EQ(prev->next, nullptr);
    prev = prev->prev;
    EXPECT_EQ(auction.insertBid("A", 8.5f, 0, prev), Auction::SUCCESS);
    check_auction_links(bids);
}

TEST(auction, remove_bid) {
    Auction auction(0);
    Auction::Bid* prev = nullptr;
    for (int i = 1; i <= 4; ++i) {
        EXPECT_EQ(auction.insertBid("A", i, 0, prev), Auction::SUCCESS);
    }
    EXPECT_EQ(auction.removeBid("", 1), Auction::BIDDER_EMPTY);
    EXPECT_EQ(auction.removeBid("A", 5), Auction::PRICE_NOT_FOUND);
    EXPECT_EQ(auction.removeBid("B", 1), Auction::BIDDER_NOT_FOUND);
    // remove middle bid
    auto& bids = auction.getBids();
    EXPECT_EQ(auction.removeBid("A", 3), Auction::SUCCESS);
    check_auction_links(bids);
    // remove last bid
    EXPECT_EQ(auction.removeBid("A", 4), Auction::SUCCESS);
    check_auction_links(bids);
    // remove first bid
    EXPECT_EQ(auction.removeBid("A", 1), Auction::SUCCESS);
    check_auction_links(bids);
}

TEST(auction, get_higher_bid) {
    Auction auction(0);
    EXPECT_EQ(auction.getHigherBid(0), auction.getBids().end());
    EXPECT_EQ(auction.getHigherBid(-1), auction.getBids().begin());
    Auction::Bid* prev_a = nullptr;
    Auction::Bid* prev_b = nullptr;
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
    EXPECT_EQ(auction.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
    EXPECT_EQ(auction.insertBid("A", 3, 0, prev_a), Auction::SUCCESS);
    EXPECT_EQ(auction.insertBid("B", 4, 0, prev_b), Auction::SUCCESS);
    for (int i = -1; i <= 3; ++i) {
        EXPECT_EQ(auction.getHigherBid(i)->first, i + 1);
    }
    EXPECT_EQ(auction.getHigherBid(4), auction.getBids().end());
    EXPECT_EQ(auction.getHigherBid(0, "A")->first, 2);
    EXPECT_EQ(auction.getHigherBid(0, "B")->first, 1);
    EXPECT_EQ(auction.getHigherBid(3, "A")->first, 4);
    EXPECT_EQ(auction.getHigherBid(3, "B"), auction.getBids().end());
}

TEST(auction, get_highest_bid) {
    Auction auction(0);
    EXPECT_EQ(auction.getHighestBid()->first, 0);
    EXPECT_EQ(auction.getHighestBid("A")->first, 0);
    Auction::Bid* prev_a = nullptr;
    Auction::Bid* prev_b = nullptr;
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
    EXPECT_EQ(auction.getHighestBid()->first, 1);
    EXPECT_EQ(auction.getHighestBid("A")->first, 0);
    EXPECT_EQ(auction.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
    EXPECT_EQ(auction.getHighestBid()->first, 2);
    EXPECT_EQ(auction.getHighestBid("A")->first, 2);
    EXPECT_EQ(auction.getHighestBid("B")->first, 1);
    EXPECT_EQ(auction.insertBid("B", 3, 0, prev_b), Auction::SUCCESS);
    EXPECT_EQ(auction.getHighestBid()->first, 3);
    EXPECT_EQ(auction.getHighestBid("A")->first, 3);
    EXPECT_EQ(auction.getHighestBid("B")->first, 1);
}

TEST(auction, detect_cycle) {
    size_t nonce = 0;
    {
        Auction auction(0);
        Auction::Bid* prev = nullptr;
        // single bid should not have any cycles
        EXPECT_EQ(auction.insertBid("A", 1, 0, prev), Auction::SUCCESS);
        auto& start_bid = auction.getBids().begin()->second;
        EXPECT_FALSE(start_bid.detectCycle(++nonce));

        // two consecutive bids in the same auction with inversed order causes cycle
        EXPECT_EQ(auction.insertBid("A", 2, 0, prev), Auction::SUCCESS);
        EXPECT_TRUE(start_bid.detectCycle(++nonce));

        // remove culptrit bid
        EXPECT_EQ(auction.removeBid("A", 2), Auction::SUCCESS);
        EXPECT_FALSE(start_bid.detectCycle(++nonce));

        // two consecutive bids in the same auction without order inversion
        prev = start_bid.higher;
        EXPECT_EQ(auction.insertBid("A", 0.5f, 0, prev), Auction::SUCCESS);
        EXPECT_FALSE(start_bid.detectCycle(++nonce));
    }
    {
        // auc1  auc2
        //  B1 <- B2
        //  ^     ^
        //  |     |
        //  A1 <- A2
        // no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
    }
    {
        // auc1  auc2
        //  B1 <- B2
        //  |     ^
        //  v     |
        //  A1 <- A2
        //  no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
    }
    {
        // auc1  auc2
        //  B2 -> B1
        //  ^     ^
        //  |     |
        //  A1 <- A2
        //  no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
    }
    {
        // auc1  auc2
        //  B2 -> B1
        //  |     ^
        //  v     |
        //  A1 <- A2
        //  no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
    }
    {
        // auc1  auc2
        //  B2 -> B1
        //  ^     |
        //  |     v
        //  A1 <- A2
        //  yes cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_TRUE(prev_a->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->detectCycle(++nonce));
    }
}

TEST(auction, total_duration) {
    Auction auction(0);
    Auction::Bid* prev = nullptr;
    for (size_t i = 1; i <= 10; ++i) {
        EXPECT_EQ(auction.insertBid("A", i, 1, prev), Auction::SUCCESS);
    }
    for (size_t i = 0; i < auction.getBids().size(); ++i) {
        EXPECT_EQ(std::next(auction.getBids().begin(), i)->second.totalDuration(), i);
    }
    // remove middle
    EXPECT_EQ(auction.removeBid("A", 5), Auction::SUCCESS);
    for (size_t i = 0; i < auction.getBids().size(); ++i) {
        EXPECT_EQ(std::next(auction.getBids().begin(), i)->second.totalDuration(), i);
    }
    // remove start
    EXPECT_EQ(auction.removeBid("A", 1), Auction::SUCCESS);
    for (size_t i = 0; i < auction.getBids().size(); ++i) {
        EXPECT_EQ(std::next(auction.getBids().begin(), i)->second.totalDuration(), i);
    }
    // remove end
    EXPECT_EQ(auction.removeBid("A", 10), Auction::SUCCESS);
    for (size_t i = 0; i < auction.getBids().size(); ++i) {
        EXPECT_EQ(std::next(auction.getBids().begin(), i)->second.totalDuration(), i);
    }
}

#if 0
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
