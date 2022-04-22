#include <decentralized_path_auction/graph.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Nodes& pathway, Point a, Point b, size_t n, Node::State state = Node::DEFAULT) {
    ASSERT_GT(n, 1u);
    auto pos = a;
    auto inc = b;
    bg::subtract_point(inc, a);
    bg::divide_value(inc, n - 1);
    pathway.clear();
    for (size_t i = 0; i < n; ++i, bg::add_point(pos, inc)) {
        auto node = graph.insertNode(pos, state);
        ASSERT_TRUE(node);
        if (i > 0) {
            node->edges.push_back(pathway.back());
            pathway.back()->edges.push_back(node);
        }
        pathway.push_back(std::move(node));
    }
}

void print_graph(const Graph& graph) {
    auto rtree = graph.getNodes();
    for (auto qit = rtree.qbegin(bg::index::nearest(Point{0, 0}, rtree.size())); qit != rtree.qend(); ++qit) {
        const auto& [pos, node] = *qit;
        printf("(%.2f, %.2f):", pos.get<0>(), pos.get<1>());
        for (const auto& adj_node : node->edges) {
            printf(" (%.2f, %.2f, %u)", adj_node->position.get<0>(), adj_node->position.get<1>(), adj_node->state);
        }
        puts("");
    }
    printf("%lu nodes total\r\n", graph.getNodes().size());
}

bool save_graph_dot(const Graph& graph, const char* file) {
    auto fp = fopen(file, "w");
    if (!fp) {
        return false;
    }
    fputs("digraph {", fp);
    for (auto& [pos, node] : graph.getNodes()) {
        fprintf(fp, "  \"%f\n%f\" [pos=\"%f,%f!\"]\r\n", pos.get<0>(), pos.get<1>(), pos.get<0>(), pos.get<1>());
        for (auto& adj_node : node->edges) {
            fprintf(fp, "    \"%f\n%f\" -> \"%f\n%f\"\r\n", pos.get<0>(), pos.get<1>(), adj_node->position.get<0>(),
                    adj_node->position.get<1>());
        }
    }
    fputs("}", fp);
    return !fclose(fp);
}

bool save_graph(const Graph& graph, const char* file) {
    auto fp = fopen(file, "w");
    if (!fp) {
        return false;
    }
    fprintf(fp, "src_pos_x, src_pos_y, dst_pos_x, dst_pos_y\r\n");
    for (auto& [pos, node] : graph.getNodes()) {
        for (auto& adj_node : node->edges) {
            fprintf(fp, "%f, %f, %f, %f\r\n", pos.get<0>(), pos.get<1>(), adj_node->position.get<0>(),
                    adj_node->position.get<1>());
        }
    }
    return !fclose(fp);
}

TEST(graph, destruct_or_clear_nodes) {
    Nodes nodes;
    {
        Graph graph;
        make_pathway(graph, nodes, {0, 0}, {1, 10}, 11);
    }
    EXPECT_FALSE(nodes.empty());
    for (auto& node : nodes) {
        EXPECT_TRUE(node->edges.empty());
        EXPECT_EQ(node->state, Node::DELETED);
        EXPECT_EQ(node.use_count(), 1);
    }
}

TEST(graph, move_assign) {
    Nodes nodes1, nodes2;
    Graph graph1, graph2;
    make_pathway(graph1, nodes1, {0, 0}, {1, 10}, 11);
    make_pathway(graph2, nodes2, {0, 0}, {1, 10}, 11);
    EXPECT_FALSE(nodes1.empty());
    EXPECT_FALSE(nodes2.empty());
    graph1 = std::move(graph2);
    for (auto& node : nodes1) {
        EXPECT_TRUE(node->edges.empty());
        EXPECT_EQ(node->state, Node::DELETED);
        EXPECT_EQ(node.use_count(), 1);
    }
    for (auto& node : nodes2) {
        EXPECT_FALSE(node->edges.empty());
        EXPECT_NE(node->state, Node::DELETED);
        EXPECT_NE(node.use_count(), 1);
    }
}

TEST(graph, insert_node) {
    Graph graph;
    NodePtr node(new Node{{0, 0}});
    // valid insert
    ASSERT_TRUE(graph.insertNode(node));
    // can't insert duplicate positions
    ASSERT_FALSE(graph.insertNode(node));
    // cant insert deleted node
    ASSERT_FALSE(graph.insertNode({1, 1}, Node::DELETED));
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
    NodePtr node(new Node{{0, 0}});
    ASSERT_FALSE(graph.removeNode(std::move(node)));
    ASSERT_FALSE(graph.removeNode(Point{0, 0}));
    // valid delete
    node = NodePtr(new Node{{0, 0}});
    node->edges.push_back(nullptr);
    ASSERT_TRUE(graph.insertNode(node));
    ASSERT_TRUE(graph.removeNode(node));
    // node should be in deleted state
    ASSERT_TRUE(node->edges.empty());
    ASSERT_EQ(node->state, Node::DELETED);
    ASSERT_EQ(node.use_count(), 1);
    // all nodes removed
    ASSERT_TRUE(graph.getNodes().empty());
}

TEST(graph, find_node) {
    Graph graph;
    EXPECT_FALSE(graph.findNode({0, 0}));
    ASSERT_TRUE(graph.insertNode(Point{0, 0}));
    EXPECT_TRUE(graph.findNode({0, 0}));
    EXPECT_TRUE(graph.findNode({0, 0}));
}

TEST(graph, find_nearest_node) {
    Graph graph;
    Nodes pathway;
    make_pathway(graph, pathway, {0, 0}, {1, 10}, 11, Node::NO_PARKING);
    EXPECT_EQ(nullptr, graph.findNearestNode({100, 13}, Node::DEFAULT));
    EXPECT_EQ(pathway.back(), graph.findNearestNode({100, 13}, Node::NO_PARKING));
    EXPECT_EQ(pathway.front(), graph.findNearestNode({-100, -13}, Node::NO_PARKING));
    EXPECT_EQ(pathway[5], graph.findNearestNode({0.51, 5.1}, Node::NO_PARKING));
}

TEST(graph, find_any_node) {
    Graph graph;
    EXPECT_FALSE(graph.findAnyNode(Node::DEFAULT));
    ASSERT_TRUE(graph.insertNode({0, 0}, Node::NO_PARKING));
    EXPECT_TRUE(graph.findAnyNode(Node::NO_PARKING));
    EXPECT_FALSE(graph.findAnyNode(Node::DEFAULT));
}

TEST(graph, contains_node) {
    Graph graph;
    EXPECT_FALSE(graph.containsNode(nullptr));
    NodePtr node(new Node{{0, 0}});
    EXPECT_FALSE(graph.containsNode(node));
    EXPECT_TRUE(graph.insertNode(node));
    EXPECT_TRUE(graph.containsNode(node));
}

TEST(graph, validate_node) {
    NodePtr node(new Node{{0, 0}});
    EXPECT_TRUE(Node::validate(node));
    node->state = Node::DELETED;
    EXPECT_FALSE(Node::validate(node));
    EXPECT_FALSE(Node::validate(nullptr));
}
