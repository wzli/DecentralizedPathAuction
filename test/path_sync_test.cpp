#include <decentralized_path_auction/path_sync.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Nodes& pathway, Point2D a, Point2D b, size_t n, Node::State state = Node::DEFAULT);

void print_path(const Path& path);

TEST(path_sync, move_assign) {
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    for (auto& node : nodes) {
        path.push_back(Visit{node, 1, 1});
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
        ASSERT_EQ(node->auction.getBids().size(), 3u);
    }
    path_sync_1 = std::move(path_sync_2);
    // expect that path in path_sync 1 is removed, and replaced with the other
    for (auto& node : nodes) {
        ASSERT_EQ(node->auction.getBids().size(), 2u);
        ASSERT_EQ(node->auction.getHighestBid()->first, 2);
    }
    ASSERT_TRUE(path_sync_2.getPaths().empty());
    ASSERT_EQ(path_sync_1.getPaths().size(), 1u);
    ASSERT_EQ(path_sync_1.getPaths().count("B"), 1u);
}

TEST(path_sync, update_path) {
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    for (auto& node : nodes) {
        path.push_back(Visit{node, 2, 1});
    }
    // input sanity checks
    PathSync path_sync;
    ASSERT_EQ(path_sync.updatePath("", path, 0), PathSync::AGENT_ID_EMPTY);
    path.back().duration *= -1;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::VISIT_DURATION_NEGATIVE);
    path.back().duration *= -1;

    path[5].node = nullptr;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::VISIT_NODE_INVALID);
    path[5].node = nodes[5];

    path[5].node->state = Node::DISABLED;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::VISIT_NODE_DISABLED);
    path[5].node->state = Node::DEFAULT;

    path[5].price *= -1;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::VISIT_PRICE_LESS_THAN_START_PRICE);
    path[5].price *= -1;

    path.push_back(path.back());
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::PATH_VISIT_DUPLICATED);
    path.pop_back();

    path.back().node->state = Node::NO_PARKING;
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::DESTINATION_NODE_NO_PARKING);
    path.back().node->state = Node::DEFAULT;

    // valid insert
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);

    // valid stale path id
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::PATH_ID_STALE);

    // reject duplicate bids
    ASSERT_EQ(path_sync.updatePath("B", path, 0), PathSync::VISIT_PRICE_ALREADY_EXIST);

    // reject source node outbid
    {
        Path path_b = {path[0]};
        --path_b[0].price;
        ASSERT_EQ(path_sync.updatePath("B", path_b, 0), PathSync::SOURCE_NODE_OUTBID);
    }

    // valid update
    for (auto& node : nodes) {
        EXPECT_EQ(node->auction.getBids().size(), 2u);
    }
    ASSERT_EQ(path_sync.updatePath("A", path, 1), PathSync::SUCCESS);
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::PATH_ID_STALE);
    for (auto& node : nodes) {
        EXPECT_EQ(node->auction.getBids().size(), 2u);
    }
    nodes[5]->auction.clearBids(0);
    ASSERT_EQ(path_sync.updatePath("A", path, 2), PathSync::VISIT_BID_ALREADY_REMOVED);

    auto& path_info = path_sync.getPaths().at("A");
    EXPECT_EQ(path_info.path_id, 2u);
    EXPECT_EQ(path_info.path.size(), path.size());
}

TEST(path_sync, update_progress) {
    Graph graph;
    Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    for (auto& node : nodes) {
        path.push_back(Visit{node, 1, 1});
    }
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    auto& path_info = path_sync.getPaths().at("A");
    EXPECT_EQ(path_info.path.size(), path.size());
    EXPECT_EQ(path_info.progress, 0u);

    // input checks
    ASSERT_EQ(path_sync.updateProgress("B", 0, 0), PathSync::AGENT_ID_NOT_FOUND);
    ASSERT_EQ(path_sync.updateProgress("A", 0, 1), PathSync::PATH_ID_MISMATCH);
    ASSERT_EQ(path_sync.updateProgress("A", path.size(), 0), PathSync::PROGRESS_EXCEED_PATH_SIZE);

    // succeed
    ASSERT_EQ(path_sync.updateProgress("A", 5, 0), PathSync::SUCCESS);
    ASSERT_EQ(path_sync.updateProgress("A", 5, 0), PathSync::SUCCESS);
    // first bids before updated progress should be removed
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), i < 5 ? 1u : 2u);
    }
    EXPECT_EQ(path_sync.updateProgress("A", 0, 0), PathSync::PROGRESS_DECREASE_DENIED);
    EXPECT_EQ(path_info.progress, 5u);
    EXPECT_EQ(path_info.path.size(), path.size());
}

TEST(path_sync, remove_path) {
    Graph graph;
    Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    for (auto& node : nodes) {
        path.push_back(Visit{node, 1, 1});
    }
    ASSERT_EQ(path_sync.removePath("B"), PathSync::AGENT_ID_NOT_FOUND);

    // insert and remove
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 2u);
    }
    ASSERT_EQ(path_sync.removePath("A"), PathSync::SUCCESS);
    ASSERT_EQ(path_sync.getPaths().count("A"), 0u);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 1u);
    }

    // insert and remove but with a missing bid during removal
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 2u);
    }
    nodes[5]->auction.clearBids(0);
    ASSERT_EQ(path_sync.removePath("A"), PathSync::VISIT_BID_ALREADY_REMOVED);
    ASSERT_EQ(path_sync.getPaths().count("A"), 0u);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 1u);
    }
}

TEST(path_sync, clear_paths) {
    Graph graph;
    Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {10, 10}, 11);
    Path path;
    for (auto& node : nodes) {
        path.push_back(Visit{node, 1, 1});
    }
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    path_sync.clearPaths();
    EXPECT_EQ(path_sync.getPaths().size(), 0u);
    for (size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->auction.getBids().size(), 1u);
    }
}

TEST(path_sync, check_wait_conditions) {
    Graph graph;
    Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {9, 9}, 10);
    Path path;
    // path a occupies every node except the last one
    for (int i = 0; i < 9; ++i) {
        path.push_back(Visit{nodes[i], 2, 1});
    }
    // null check
    std::tuple<PathSync::Error, size_t, float> ret;
    auto& [error, blocked_progress, remaining_duration] = ret;
    ret = path_sync.checkWaitConditions("A");
    ASSERT_EQ(error, PathSync::AGENT_ID_NOT_FOUND);

    // add whole path with no competition
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);

    // path validation checks
    nodes[5]->state = Node::DELETED;
    ret = path_sync.checkWaitConditions("A");
    EXPECT_EQ(error, PathSync::VISIT_NODE_INVALID);
    EXPECT_EQ(blocked_progress, 5u);
    EXPECT_EQ(remaining_duration, FLT_MAX);
    nodes[5]->state = Node::DEFAULT;

    nodes[5]->state = Node::DISABLED;
    ret = path_sync.checkWaitConditions("A");
    EXPECT_EQ(error, PathSync::VISIT_NODE_DISABLED);
    EXPECT_EQ(blocked_progress, 5u);
    EXPECT_EQ(remaining_duration, FLT_MAX);
    nodes[5]->state = Node::DEFAULT;

    path.back().node->state = Node::NO_PARKING;
    ret = path_sync.checkWaitConditions("A");
    EXPECT_EQ(error, PathSync::DESTINATION_NODE_NO_PARKING);
    EXPECT_EQ(blocked_progress, path.size() - 1);
    EXPECT_EQ(remaining_duration, FLT_MAX);
    path.back().node->state = Node::DEFAULT;

    // success case
    ret = path_sync.checkWaitConditions("A");
    EXPECT_EQ(error, PathSync::SUCCESS);
    EXPECT_EQ(blocked_progress, path.size());
    EXPECT_EQ(remaining_duration, path.size() - 1);

    // intersecting path with lower bid
    {
        Path path_b = {{nodes[9], 2, 1}, {nodes[8], 1, 1}};
        ASSERT_EQ(path_sync.updatePath("B", path_b, 0), PathSync::SUCCESS);
        ret = path_sync.checkWaitConditions("A");
        EXPECT_EQ(error, PathSync::SUCCESS);
        EXPECT_EQ(blocked_progress, path.size());
        EXPECT_EQ(remaining_duration, path.size() - 1);
    }

    // intersecting path with higher bid
    {
        Path path_b = {{nodes[9], 2, 1}, {nodes[8], 3, 1}};
        ASSERT_EQ(path_sync.updatePath("B", path_b, 1), PathSync::SUCCESS);
        ret = path_sync.checkWaitConditions("A");
        EXPECT_EQ(error, PathSync::SUCCESS);
        EXPECT_EQ(blocked_progress, path.size() - 1);
        EXPECT_EQ(remaining_duration, path.size() - 1);
    }

    // start node outbid
    ASSERT_EQ(path_sync.updatePath("C", {{nodes[0], 3}}, 0), PathSync::SUCCESS);
    ret = path_sync.checkWaitConditions("A");
    EXPECT_EQ(error, PathSync::SOURCE_NODE_OUTBID);
    EXPECT_EQ(blocked_progress, 0u);

    // detect externally removed bid
    nodes[7]->auction.clearBids(0);
    ret = path_sync.checkWaitConditions("A");
    EXPECT_EQ(error, PathSync::VISIT_BID_ALREADY_REMOVED);
    EXPECT_EQ(blocked_progress, 7u);
    EXPECT_EQ(remaining_duration, FLT_MAX);
}

TEST(path_sync, entited_segment) {
    Graph graph;
    Nodes nodes;
    PathSync path_sync;
    make_pathway(graph, nodes, {0, 0}, {9, 9}, 10);
    Path path;
    for (int i = 0; i < 9; ++i) {
        path.push_back(Visit{nodes[i], 2, 1});
    }
    // null check
    Path segment;
    ASSERT_EQ(path_sync.getEntitledSegment("A", segment), PathSync::AGENT_ID_NOT_FOUND);
    // whole path with no competition
    ASSERT_EQ(path_sync.updatePath("A", path, 0), PathSync::SUCCESS);
    EXPECT_EQ(path_sync.getEntitledSegment("A", segment), PathSync::SUCCESS);
    EXPECT_EQ(segment.size(), path.size());
    nodes[5]->state = Node::DISABLED;
    EXPECT_EQ(path_sync.getEntitledSegment("A", segment), PathSync::VISIT_NODE_DISABLED);
    EXPECT_EQ(segment.size(), 5u);
    nodes[5]->state = Node::DEFAULT;

    // intersecting path with lower bid
    {
        Path path_b = {{nodes[9], 2, 1}, {nodes[8], 1, 1}};
        ASSERT_EQ(path_sync.updatePath("B", path_b, 0), PathSync::SUCCESS);
        EXPECT_EQ(path_sync.getEntitledSegment("A", segment), PathSync::SUCCESS);
        EXPECT_EQ(segment.size(), path.size());
    }

    // intersecting path with higher bid
    {
        Path path_b = {{nodes[9], 2, 1}, {nodes[8], 3, 1}};
        ASSERT_EQ(path_sync.updatePath("B", path_b, 1), PathSync::SUCCESS);
        EXPECT_EQ(path_sync.getEntitledSegment("A", segment), PathSync::SUCCESS);
        EXPECT_EQ(segment.size(), path.size() - 1);
    }

    // start node outbid
    ASSERT_EQ(path_sync.updatePath("C", {{nodes[0], 3}}, 0), PathSync::SUCCESS);
    EXPECT_EQ(path_sync.getEntitledSegment("A", segment), PathSync::SOURCE_NODE_OUTBID);
    EXPECT_EQ(segment.size(), 0u);
}

TEST(path_sync, detect_cycle) {
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {1, 1}, 2);

    // expect cycles
    {
        PathSync path_sync;
        Path path_a = {{nodes[0], 1}, {nodes[1], 1}};
        Path path_b = {{nodes[0], 2}, {nodes[1], 2}};
        ASSERT_EQ(path_sync.updatePath("A", path_a, 0), PathSync::SUCCESS);
        ASSERT_EQ(path_sync.updatePath("B", path_b, 0), PathSync::SUCCESS);
    }

    {
        PathSync path_sync;
        Path path_a = {{nodes[0], 1}, {nodes[1], 1}};
        Path path_b = {{nodes[1], 2}, {nodes[0], 2}};
        ASSERT_EQ(path_sync.updatePath("A", path_a, 0), PathSync::SUCCESS);
        ASSERT_EQ(path_sync.updatePath("B", path_b, 0), PathSync::SUCCESS);
    }
    // expect cycles
    {
        PathSync path_sync;
        Path path_a = {{nodes[0], 1}, {nodes[1], 2}};
        Path path_b = {{nodes[0], 2}, {nodes[1], 1}};
        ASSERT_EQ(path_sync.updatePath("A", path_a, 0), PathSync::SUCCESS);
        ASSERT_EQ(path_sync.updatePath("B", path_b, 0), PathSync::PATH_CAUSES_CYCLE);
    }
    {
        PathSync path_sync;
        Path path_a = {{nodes[0], 2}, {nodes[1], 3}};
        Path path_b = {{nodes[1], 4}, {nodes[0], 1}};
        ASSERT_EQ(path_sync.updatePath("A", path_a, 0), PathSync::SUCCESS);
        ASSERT_EQ(path_sync.updatePath("B", path_b, 0), PathSync::PATH_CAUSES_CYCLE);
    }
}
