#include <decentralized_path_auction/path_sync.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Graph::Nodes& pathway, Point2D a, Point2D b, size_t n,
        Graph::Node::State state = Graph::Node::ENABLED);

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
