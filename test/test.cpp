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

bool save_graph(const Graph& graph, const char* file) {
    auto fp = fopen(file, "w");
    if (!fp) {
        return false;
    }
    fputs("digraph {", fp);
    for (auto& [pos, node] : graph.getNodes()) {
        fprintf(fp, "  \"%f\n%f\" [pos=\"%f,%f!\"]\r\n", pos.x(), pos.y(), pos.x(), pos.y());
        for (auto& adj_node : node->edges) {
            fprintf(fp, "    \"%f\n%f\" -> \"%f\n%f\"\r\n", pos.x(), pos.y(), adj_node->position.x(),
                    adj_node->position.y());
        }
    }
    fputs("}", fp);
    return !fclose(fp);
}

void print_path(const Path& path) {
    for (auto& visit : path) {
        printf("{[%.2f %.2f], t: %.2f, p: %.2f, b: %.2f, c: %f}\r\n", visit.node->position.x(),
                visit.node->position.y(), visit.time,
                visit.price >= std::numeric_limits<float>::max() ? std::numeric_limits<float>::infinity() : visit.price,
                visit.base_price, visit.node->auction.getBids().find(visit.base_price)->second.cost_estimate);
    }
    puts("");
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
    EXPECT_EQ(start_bid.lower, nullptr);
}

////////////////////////////////////////////////////////////////////////////////

void check_auction_links(const Auction::Bids& bids) {
    EXPECT_EQ(bids.rbegin()->second.next, nullptr);
    EXPECT_EQ(bids.begin()->second.lower, nullptr);
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
        ASSERT_TRUE(bid->lower);
        EXPECT_EQ(bid->lower->next, bid->lower == &bids.begin()->second ? nullptr : bid);
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
    EXPECT_EQ(bids.begin()->second.lower, nullptr);
    EXPECT_EQ(prev->prev, nullptr);
    EXPECT_EQ(prev->next, nullptr);
    EXPECT_EQ(prev->lower, &bids.begin()->second);
    // more rejection checks
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev), Auction::PRICE_ALREADY_EXIST);
    EXPECT_EQ(auction.insertBid("B", 2, 0, prev), Auction::BIDDER_MISMATCH);
    // add chained bid
    for (int i = 2; i < 10; ++i) {
        EXPECT_EQ(auction.insertBid("A", i, 0, prev), Auction::SUCCESS);
    }
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
        EXPECT_FALSE(prev->detectCycle(++nonce));

        // two consecutive bids in the same auction with inversed order causes cycle
        EXPECT_EQ(auction.insertBid("A", 2, 0, prev), Auction::SUCCESS);
        EXPECT_TRUE(prev->detectCycle(++nonce));
        EXPECT_TRUE(prev->prev->detectCycle(++nonce));

        // remove culptrit bid
        prev = prev->prev;
        EXPECT_EQ(auction.removeBid("A", 2), Auction::SUCCESS);
        EXPECT_FALSE(prev->detectCycle(++nonce));

        // two consecutive bids in the same auction without order inversion still causes cycle
        EXPECT_EQ(auction.insertBid("A", 0.5f, 0, prev), Auction::SUCCESS);
        EXPECT_TRUE(prev->detectCycle(++nonce));
        EXPECT_TRUE(prev->prev->detectCycle(++nonce));
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
        EXPECT_FALSE(prev_a->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(++nonce));
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
        EXPECT_FALSE(prev_a->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(++nonce));
    }
    {
        // auc1  auc2
        //  B1 <- B2
        //  |     ^
        //  v     |
        //  A1 <- A2
        //  yes cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_TRUE(prev_a->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->detectCycle(++nonce));
        EXPECT_TRUE(prev_a->prev->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(++nonce));
    }
    {
        // auc1  auc2
        //  B2 -> B1
        //  |     ^
        //  v     |
        //  A1 <- A2
        //  yes cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_TRUE(prev_a->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->detectCycle(++nonce));
        EXPECT_TRUE(prev_a->prev->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(++nonce));
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
        EXPECT_TRUE(prev_a->prev->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(++nonce));
    }
    {
        // auc1        auc2
        //  B1 <- B2 <- B3
        //  ^           ^
        //  |           |
        //  A1 <- A2 <- A3
        // no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction auca(0);
        Auction aucb(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auca.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(aucb.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(++nonce));
    }
    {
        // auc1        auc2
        //  B1 <- B2 <- B3
        //  ^           |
        //  |           v
        //  A1 <- A2 <- A3
        // no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction auca(0);
        Auction aucb(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auca.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(aucb.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(++nonce));
    }
    {
        // auc1        auc2
        //  B3 -> B2 -> B1
        //  ^           ^
        //  |           |
        //  A1 <- A2 <- A3
        // no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction auca(0);
        Auction aucb(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auca.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(aucb.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(++nonce));
    }
    {
        // auc1        auc2
        //  B3 -> B2 -> B1
        //  |           ^
        //  v           |
        //  A1 <- A2 <- A3
        // no cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction auca(0);
        Auction aucb(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auca.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(aucb.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_FALSE(prev_a->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(++nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(++nonce));
    }
    {
        // auc1        auc2
        //  B1 -> B2 -> B3
        //  ^           |
        //  |           v
        //  A1 <- A2 <- A3
        // yes cycle
        Auction auc1(0);
        Auction auc2(0);
        Auction auca(0);
        Auction aucb(0);
        Auction::Bid* prev_a = nullptr;
        Auction::Bid* prev_b = nullptr;
        EXPECT_EQ(auc1.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auca.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("A", 2, 0, prev_a), Auction::SUCCESS);
        EXPECT_EQ(auc2.insertBid("B", 1, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(aucb.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_EQ(auc1.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
        EXPECT_TRUE(prev_a->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->detectCycle(++nonce));
        EXPECT_TRUE(prev_a->prev->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(++nonce));
        EXPECT_TRUE(prev_a->prev->prev->detectCycle(++nonce));
        EXPECT_TRUE(prev_b->prev->prev->detectCycle(++nonce));
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

////////////////////////////////////////////////////////////////////////////////

TEST(path_sync, move_assign) {
    Graph graph;
    Graph::Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    float t = 0;
    for (auto& node : nodes) {
        path.push_back(Visit{node, t++, 1, 0});
    }
    // Add two paths to two path_sync with the same graph
    PathSync path_sync_1;
    ASSERT_EQ(path_sync_1.updatePath("A", path, 0), PathSync::SUCCESS);
    for (auto& visit : path) {
        ++visit.price;
    }
    PathSync path_sync_2;
    ASSERT_EQ(path_sync_2.updatePath("B", path, 0), PathSync::SUCCESS);
    for (auto& node : nodes) {
        ASSERT_EQ(node->auction.getBids().size(), 3);
    }
    path_sync_1 = std::move(path_sync_2);
    // expect that path in path_sync 1 is removed, and replaced with the other
    for (auto& node : nodes) {
        ASSERT_EQ(node->auction.getBids().size(), 2);
        ASSERT_EQ(node->auction.getHighestBid()->first, 2);
    }
    ASSERT_TRUE(path_sync_2.getPaths().empty());
    ASSERT_EQ(path_sync_1.getPaths().size(), 1);
    ASSERT_EQ(path_sync_1.getPaths().count("B"), 1);
}

TEST(path_sync, update_path) {
    Graph graph;
    Graph::Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    float t = 0;
    for (auto& node : nodes) {
        path.push_back(Visit{node, t++, 1, 0});
    }
    // input sanity checks
    PathSync path_sync;
    ASSERT_EQ(path_sync.updatePath("", path, 0), PathSync::AGENT_ID_EMPTY);
    ASSERT_EQ(path_sync.updatePath("A", path, 0, -1), PathSync::STOP_DURATION_NEGATIVE);

    path[5].node = nullptr;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::VISIT_NODE_INVALID);
    path[5].node = nodes[5];

    path[5].price = 0;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::VISIT_PRICE_NOT_ABOVE_BASE_PRICE);
    path[5].price = 1;

    path[5].base_price = -1;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::VISIT_MIN_PRICE_LESS_THAN_START_PRICE);
    path[5].base_price = 0;

    path[5].time *= -1;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::PATH_TIME_DECREASED);
    path[5].time *= -1;

    // valid insert
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);

    // valid stale path id
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::PATH_ID_STALE);

    // reject duplicate bids
    ASSERT_EQ(path_sync.updatePath("B", path, 0), PathSync::VISIT_PRICE_ALREADY_EXIST);

    // valid update
    for (auto& node : nodes) {
        EXPECT_EQ(node->auction.getBids().size(), 2);
    }
    ASSERT_EQ(path_sync.updatePath("A", path, 1), PathSync::SUCCESS);
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::PATH_ID_STALE);
    for (auto& node : nodes) {
        EXPECT_EQ(node->auction.getBids().size(), 2);
    }
    nodes[5]->auction.clearBids(0);
    ASSERT_EQ(path_sync.updatePath("A", path, 2), PathSync::VISIT_BID_ALREADY_REMOVED);

    auto& path_info = path_sync.getPaths().at("A");
    EXPECT_EQ(path_info.path_id, 2);
    EXPECT_EQ(path_info.path.size(), path.size());
}

TEST(path_sync, update_progress) {
    Graph graph;
    Graph::Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    float t = 0;
    for (auto& node : nodes) {
        path.push_back(Visit{node, t++, 1, 0});
    }
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    auto& path_info = path_sync.getPaths().at("A");
    EXPECT_EQ(path_info.path.size(), path.size());
    EXPECT_EQ(path_info.progress, 0);

    // input checks
    ASSERT_EQ(path_sync.updateProgress("B", 0, 0), PathSync::AGENT_ID_NOT_FOUND);
    ASSERT_EQ(path_sync.updateProgress("A", 0, 1), PathSync::PATH_ID_MISMATCH);
    ASSERT_EQ(path_sync.updateProgress("A", path.size(), 0), PathSync::PROGRESS_EXCEED_PATH_SIZE);

    // succeed
    ASSERT_EQ(path_sync.updateProgress("A", 5, 0), PathSync::SUCCESS);
    ASSERT_EQ(path_sync.updateProgress("A", 5, 0), PathSync::SUCCESS);
    // first bids before updated progress should be removed
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), i < 5 ? 1 : 2);
    }
    EXPECT_EQ(path_sync.updateProgress("A", 0, 0), PathSync::PROGRESS_DECREASE_DENIED);
    EXPECT_EQ(path_info.progress, 5);
    EXPECT_EQ(path_info.path.size(), path.size());
}

TEST(path_sync, remove_path) {
    Graph graph;
    Graph::Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    float t = 0;
    for (auto& node : nodes) {
        path.push_back(Visit{node, t++, 1, 0});
    }
    ASSERT_EQ(path_sync.removePath("B"), PathSync::AGENT_ID_NOT_FOUND);

    // insert and remove
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 2);
    }
    ASSERT_EQ(path_sync.removePath("A"), PathSync::SUCCESS);
    ASSERT_EQ(path_sync.getPaths().count("A"), 0);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 1);
    }

    // insert and remove but with a missing bid during removal
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 2);
    }
    nodes[5]->auction.clearBids(0);
    ASSERT_EQ(path_sync.removePath("A"), PathSync::VISIT_BID_ALREADY_REMOVED);
    ASSERT_EQ(path_sync.getPaths().count("A"), 0);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 1);
    }
}

TEST(path_sync, clear_paths) {
    Graph graph;
    Graph::Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    float t = 0;
    for (auto& node : nodes) {
        path.push_back(Visit{node, t++, 1, 0});
    }
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    path_sync.clearPaths();
    EXPECT_EQ(path_sync.getPaths().size(), 0);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 1);
    }
}

////////////////////////////////////////////////////////////////////////////////

TEST(path_search, set_destination_checks) {
    PathSearch path_search({"A"});
    Graph::NodePtr node(new Graph::Node{{0, 0}});
    // valid destination
    EXPECT_EQ(path_search.setDestination({node}), PathSearch::SUCCESS);
    // passive destination
    EXPECT_EQ(path_search.setDestination({}), PathSearch::SUCCESS);
    // invalid destination
    EXPECT_EQ(path_search.setDestination({nullptr}), PathSearch::DESTINATION_NODE_INVALID);
    // reject duplicate node
    EXPECT_EQ(path_search.setDestination({node, node}), PathSearch::DESTINATION_NODE_DUPLICATED);
    // reject no parking
    node->state = Graph::Node::NO_PARKING;
    EXPECT_EQ(path_search.setDestination({node}), PathSearch::DESTINATION_NODE_NO_PARKING);
}

TEST(path_search, iterate_search_checks) {
    PathSearch path_search({"A"});
    Graph::NodePtr node(new Graph::Node{{0, 0}});
    Path path = {{node}};

    // config validation
    auto& config = path_search.editConfig();

    config.agent_id.clear();
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::CONFIG_AGENT_ID_EMPTY);
    config.agent_id = "A";

    config.price_increment = 0;
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::CONFIG_PRICE_INCREMENT_NON_POSITIVE);
    config.price_increment = 1;

    config.time_exchange_rate = 0;
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::CONFIG_TIME_EXCHANGE_RATE_NON_POSITIVE);
    config.time_exchange_rate = 1;

    config.travel_time = nullptr;
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::CONFIG_TRAVEL_TIME_MISSING);
    config.travel_time = PathSearch::travelDistance;

    // check source node
    path.clear();
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::SOURCE_NODE_NOT_PROVIDED);
    path.push_back({nullptr});
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::SOURCE_NODE_INVALID);
    path = {{node}};
    node->state = Graph::Node::DISABLED;
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::SOURCE_NODE_DISABLED);
    node->state = Graph::Node::ENABLED;
    Auction::Bid* prev = nullptr;
    ASSERT_EQ(node->auction.insertBid("B", std::numeric_limits<float>::max(), 0, prev), Auction::SUCCESS);
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::SOURCE_NODE_OCCUPIED);
    ASSERT_EQ(node->auction.removeBid("B", std::numeric_limits<float>::max()), Auction::SUCCESS);
    // check success
    EXPECT_EQ(path_search.iterateSearch(path), PathSearch::SUCCESS);
}

Graph test_graph;
std::vector<Graph::Nodes> test_nodes;

TEST(path_search, make_test_graph) {
    /*
        make the graph below:
        00-01-02-03-04-05-06-07-08-09
        |
        10-11-12-13-14-15-16-17-18-19
        |
        20-21-22-23-24-25-26-27-28-29
    */
    auto& rows = test_nodes;
    rows.resize(3);
    make_pathway(test_graph, rows[0], {0, 0}, {90, 0}, 10);
    make_pathway(test_graph, rows[1], {0, 10}, {90, 10}, 10);
    make_pathway(test_graph, rows[2], {0, 20}, {90, 20}, 10);
    rows[0][0]->edges.push_back(rows[1][0]);
    rows[1][0]->edges.push_back(rows[0][0]);
    rows[1][0]->edges.push_back(rows[2][0]);
    rows[2][0]->edges.push_back(rows[1][0]);
    // save_graph(graph, "graph.gv");
}

TEST(path_search, single_passive_path) {
    PathSearch path_search({"A"});
    // set all nodes as no parking execpt 1
    for (auto& row : test_nodes) {
        for (auto& node : row) {
            node->state = Graph::Node::NO_PARKING;
        }
    }
    test_nodes[2][9]->state = Graph::Node::ENABLED;

    int calls;
    // try to find a passive path to the single parkable node
    Path path = {{test_nodes[0][0]}};
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterateSearch(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 164);
    EXPECT_EQ(path.back().node, test_nodes[2][9]);

    // expect a second attempt to take less iterations via previously cached cost estimates
    path.resize(1);
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterateSearch(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 11);
    EXPECT_EQ(path.back().node, test_nodes[2][9]);

    // try from a different start location
    path = {{test_nodes[0][5]}};
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterateSearch(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 38);
    EXPECT_EQ(path.back().node, test_nodes[2][9]);

    // reset node states to enabled
    for (auto& row : test_nodes) {
        for (auto& node : row) {
            node->state = Graph::Node::ENABLED;
        }
    }
}

TEST(path_search, single_passive_path_iterations) {
    PathSearch path_search({"B"});
    // set all nodes as no parking execpt 1
    for (auto& row : test_nodes) {
        for (auto& node : row) {
            node->state = Graph::Node::NO_PARKING;
        }
    }
    test_nodes[2][9]->state = Graph::Node::ENABLED;

    // find the only node that allows parking
    Path path = {{test_nodes[0][0]}};
    ASSERT_EQ(path_search.iterateSearch(path, 400), PathSearch::SUCCESS);
    EXPECT_EQ(path.back().node, test_nodes[2][9]);

    // expect a second attempt to take less iterations via previously cached cost estimates
    path.resize(1);
    ASSERT_EQ(path_search.iterateSearch(path, 20), PathSearch::SUCCESS);
    EXPECT_EQ(path.back().node, test_nodes[2][9]);

    // try from a different start location
    path = {{test_nodes[0][5]}};
    ASSERT_EQ(path_search.iterateSearch(path, 100), PathSearch::SUCCESS);
    EXPECT_EQ(path.back().node, test_nodes[2][9]);

    // reset node states to enabled
    for (auto& row : test_nodes) {
        for (auto& node : row) {
            node->state = Graph::Node::ENABLED;
        }
    }
}

// TODO:
// test passive path
// test evading into aile
// test cyclic dependencies check
// test back and forth path wait dependencies

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
