#include <decentralized_path_auction/graph.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Graph::Nodes& pathway, Point2D a, Point2D b, size_t n,
        Graph::Node::State state = Graph::Node::ENABLED) {
    ASSERT_GT(n, 1u);
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
        printf("{[%.2f %.2f], t: %.2f, p: %.2f, b: %.2f}\r\n", visit.node->position.x(), visit.node->position.y(),
                visit.time, visit.price, visit.base_price);
    }
    puts("");
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
    ASSERT_EQ(graph.getNodes().size(), 1u);
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
