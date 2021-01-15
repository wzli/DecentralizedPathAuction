#include <decentralized_path_auction/graph.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

std::vector<Graph::NodePtr> make_pathway(Graph& graph, Point2D a, Point2D b, size_t n) {
    assert(n > 1);
    auto pos = a;
    auto inc = b;
    bg::subtract_point(inc, a);
    bg::divide_value(inc, n - 1);
    std::vector<Graph::NodePtr> nodes;
    for (size_t i = 0; i < n; ++i, bg::add_point(pos, inc)) {
        auto node = graph.insertNode(pos);
        if (i > 0) {
            node->edges.push_back(nodes.back());
            nodes.back()->edges.push_back(node);
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
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
    auto pathway = make_pathway(graph, {0, 0}, {1, 10}, 11);
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

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
