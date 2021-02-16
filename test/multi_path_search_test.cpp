#include <decentralized_path_auction/path_search.hpp>
#include <decentralized_path_auction/path_sync.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Graph::Nodes& pathway, Point2D a, Point2D b, size_t n,
        Graph::Node::State state = Graph::Node::ENABLED);

// see single path search test for graph definition
std::vector<Graph::Nodes> make_test_graph(Graph& graph);

void print_path(const Path& path) {
    for (auto& visit : path) {
        printf("{[%.2f %.2f], t: %.2f, p: %.2f, b: %.2f}\r\n", visit.node->position.x(), visit.node->position.y(),
                visit.time, visit.price, visit.base_price);
    }
    puts("");
}

// TODO:
// test evading into aile
// test back and forth path wait dependencies

TEST(multi_path_search, head_on_collision) {
    Graph graph;
    Graph::Nodes nodes;
    // 0-1-2-3-4-5-6-7-8-9
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    PathSync path_sync;
    size_t path_id_a = 0;
    size_t path_id_b = 0;
    PathSearch path_search_a({"A"});
    PathSearch path_search_b({"B"});
    Path path_a = {{nodes[0]}};
    Path path_b = {{nodes[9]}};
    ASSERT_EQ(path_search_a.reset({nodes[7]}), PathSearch::SUCCESS);
    ASSERT_EQ(path_search_b.reset({nodes[2]}), PathSearch::SUCCESS);

    ASSERT_EQ(path_search_a.iterate(path_a, 100), PathSearch::SUCCESS);
    ASSERT_EQ(path_a.back().node, nodes[7]);
    ASSERT_EQ(path_sync.updatePath("A", path_a, path_id_a++), PathSync::SUCCESS);
    // print_path(path_a);

    ASSERT_EQ(path_search_b.iterate(path_b, 100), PathSearch::SUCCESS);
    // print_path(path_b);
    ASSERT_EQ(path_sync.updatePath("B", path_b, path_id_b++), PathSync::SUCCESS);

    std::vector<bool> cycle_visits;
    EXPECT_EQ(path_a.front().node->auction.getBids().find(path_a.front().price)->second.detectCycle(cycle_visits),
            Auction::SUCCESS);
    cycle_visits.clear();
    EXPECT_EQ(path_b.front().node->auction.getBids().find(path_b.front().price)->second.detectCycle(cycle_visits),
            Auction::SUCCESS);
}

TEST(multi_path_search, follow) {
    Graph graph;
    Graph::Nodes nodes;
    // 0-1-2-3-4-5-6-7-8-9
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    PathSync path_sync;
    size_t path_id_a = 0;
    size_t path_id_b = 0;
    PathSearch path_search_a({"A"});
    PathSearch path_search_b({"B"});
    Path path_a = {{nodes[1]}};
    Path path_b = {{nodes[0]}};
    ASSERT_EQ(path_search_a.reset({nodes[9]}), PathSearch::SUCCESS);
    ASSERT_EQ(path_search_b.reset({nodes[8]}), PathSearch::SUCCESS);

    ASSERT_EQ(path_search_a.iterate(path_a, 100), PathSearch::SUCCESS);
    ASSERT_EQ(path_a.back().node, nodes[9]);
    // print_path(path_a);
    ASSERT_EQ(path_sync.updatePath("A", path_a, path_id_a++), PathSync::SUCCESS);

    ASSERT_EQ(path_search_b.iterate(path_b, 100), PathSearch::SUCCESS);
    // print_path(path_b);
    ASSERT_EQ(path_b.back().node, nodes[8]);
    ASSERT_EQ(path_sync.updatePath("B", path_b, path_id_b++), PathSync::SUCCESS);

    std::vector<bool> cycle_visits;
    EXPECT_EQ(path_a.front().node->auction.getBids().find(path_a.front().price)->second.detectCycle(cycle_visits),
            Auction::SUCCESS);
    cycle_visits.clear();
    EXPECT_EQ(path_b.front().node->auction.getBids().find(path_b.front().price)->second.detectCycle(cycle_visits),
            Auction::SUCCESS);
}

TEST(multi_path_search, push) {
    Graph graph;
    Graph::Nodes nodes;
    // 0-1-2-3-4-5-6-7-8-9
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    PathSync path_sync;
    size_t path_id_a = 0;
    size_t path_id_b = 0;
    PathSearch path_search_a({"A"});
    PathSearch path_search_b({"B"});
    Path path_a = {{nodes[0]}};
    Path path_b = {{nodes[1]}};
    ASSERT_EQ(path_search_a.reset({nodes[8]}), PathSearch::SUCCESS);
    ASSERT_EQ(path_search_b.reset({}), PathSearch::SUCCESS);

    ASSERT_EQ(path_search_a.iterate(path_a, 100), PathSearch::SUCCESS);
    ASSERT_EQ(path_a.back().node, nodes[8]);
    // print_path(path_a);
    ASSERT_EQ(path_sync.updatePath("A", path_a, path_id_a++), PathSync::SUCCESS);

    ASSERT_EQ(path_search_b.iterate(path_b, 200), PathSearch::SUCCESS);
    // print_path(path_b);
    ASSERT_EQ(path_b.back().node, nodes[9]);
    ASSERT_EQ(path_sync.updatePath("B", path_b, path_id_b++), PathSync::SUCCESS);

    std::vector<bool> cycle_visits;
    EXPECT_EQ(path_a.front().node->auction.getBids().find(path_a.front().price)->second.detectCycle(cycle_visits),
            Auction::SUCCESS);
    cycle_visits.clear();
    EXPECT_EQ(path_b.front().node->auction.getBids().find(path_b.front().price)->second.detectCycle(cycle_visits),
            Auction::SUCCESS);
}
