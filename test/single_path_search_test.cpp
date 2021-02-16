#include <decentralized_path_auction/path_search.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Graph::Nodes& pathway, Point2D a, Point2D b, size_t n,
        Graph::Node::State state = Graph::Node::ENABLED);

std::vector<Graph::Nodes> make_test_graph(Graph& graph) {
    /*
        make the graph below:
        00-01-02-03-04-05-06-07-08-09
        |
        10-11-12-13-14-15-16-17-18-19
        |
        20-21-22-23-24-25-26-27-28-29
    */
    std::vector<Graph::Nodes> rows(3);
    // graph.clearNodes();
    make_pathway(graph, rows[0], {0, 0}, {90, 0}, 10);
    make_pathway(graph, rows[1], {0, 10}, {90, 10}, 10);
    make_pathway(graph, rows[2], {0, 20}, {90, 20}, 10);
    rows[0][0]->edges.push_back(rows[1][0]);
    rows[1][0]->edges.push_back(rows[0][0]);
    rows[1][0]->edges.push_back(rows[2][0]);
    rows[2][0]->edges.push_back(rows[1][0]);
    return rows;
}

TEST(path_search, reset_input_checks) {
    PathSearch path_search({"A"});
    Graph::NodePtr node(new Graph::Node{{0, 0}});
    // valid destination
    EXPECT_EQ(path_search.reset({node}), PathSearch::SUCCESS);
    // passive destination
    EXPECT_EQ(path_search.reset({}), PathSearch::SUCCESS);
    // invalid destination
    EXPECT_EQ(path_search.reset({nullptr}), PathSearch::DESTINATION_NODE_INVALID);
    // reject duplicate node
    EXPECT_EQ(path_search.reset({node, node}), PathSearch::DESTINATION_NODE_DUPLICATED);
    // reject no parking
    node->state = Graph::Node::NO_PARKING;
    EXPECT_EQ(path_search.reset({node}), PathSearch::DESTINATION_NODE_NO_PARKING);
}

TEST(path_search, iterate_input_checks) {
    PathSearch path_search({"A"});
    Graph::NodePtr node(new Graph::Node{{0, 0}});
    Path path = {{node}};

    // config validation
    auto& config = path_search.editConfig();

    config.agent_id.clear();
    EXPECT_EQ(path_search.iterate(path), PathSearch::CONFIG_AGENT_ID_EMPTY);
    config.agent_id = "A";

    config.price_increment = 0;
    EXPECT_EQ(path_search.iterate(path), PathSearch::CONFIG_PRICE_INCREMENT_NON_POSITIVE);
    config.price_increment = 1;

    config.time_exchange_rate = 0;
    EXPECT_EQ(path_search.iterate(path), PathSearch::CONFIG_TIME_EXCHANGE_RATE_NON_POSITIVE);
    config.time_exchange_rate = 1;

    config.travel_time = nullptr;
    EXPECT_EQ(path_search.iterate(path), PathSearch::CONFIG_TRAVEL_TIME_MISSING);
    config.travel_time = PathSearch::travelDistance;

    // check source node
    path.clear();
    EXPECT_EQ(path_search.iterate(path), PathSearch::SOURCE_NODE_NOT_PROVIDED);
    path.push_back({nullptr});
    EXPECT_EQ(path_search.iterate(path), PathSearch::SOURCE_NODE_INVALID);
    path = {{node}};
    node->state = Graph::Node::DISABLED;
    EXPECT_EQ(path_search.iterate(path), PathSearch::SOURCE_NODE_DISABLED);
    node->state = Graph::Node::ENABLED;
    Auction::Bid* prev = nullptr;
    ASSERT_EQ(node->auction.insertBid("B", std::numeric_limits<float>::max(), 0, prev), Auction::SUCCESS);
    EXPECT_EQ(path_search.iterate(path), PathSearch::SOURCE_NODE_OCCUPIED);
    ASSERT_EQ(node->auction.removeBid("B", std::numeric_limits<float>::max()), Auction::SUCCESS);
    // check success
    EXPECT_EQ(path_search.iterate(path), PathSearch::SUCCESS);
}

TEST(path_search, single_path) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    PathSearch path_search({"A"});
    // try to find the shortest path
    ASSERT_EQ(path_search.reset({nodes[2][5]}), PathSearch::SUCCESS);
    Path path = {{nodes[0][5]}};
    int calls;
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterate(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 46);
    EXPECT_EQ(path.back().node, nodes[2][5]);

    // expect a second attempt to take less iterations via previously cached cost estimates
    path.resize(1);
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterate(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 12);
    EXPECT_EQ(path.back().node, nodes[2][5]);

    // try from a different start location
    path = {{nodes[0][9]}};
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterate(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 16);
    EXPECT_EQ(path.back().node, nodes[2][5]);

    // try a source and different destination
    ASSERT_EQ(path_search.reset({nodes[1][5]}), PathSearch::SUCCESS);
    path = {{nodes[2][9]}};
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterate(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 15);
    EXPECT_EQ(path.back().node, nodes[1][5]);
}

TEST(path_search, single_passive_path) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    PathSearch path_search({"A"});
    // set all nodes as no parking execpt 1
    for (int i = 0; i < 3; ++i) {
        for (auto& node : nodes[i]) {
            node->state = Graph::Node::NO_PARKING;
        }
    }
    nodes[2][9]->state = Graph::Node::ENABLED;

    int calls;
    // try to find a passive path to the single parkable node
    Path path = {{nodes[0][0]}};
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterate(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 164);
    EXPECT_EQ(path.back().node, nodes[2][9]);

    // expect a second attempt to take less iterations via previously cached cost estimates
    path.resize(1);
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterate(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 11);
    EXPECT_EQ(path.back().node, nodes[2][9]);

    // try from a different start location
    path = {{nodes[0][5]}};
    for (calls = 0; calls < 200; ++calls) {
        auto error = path_search.iterate(path);
        if (error == PathSearch::SUCCESS) {
            break;
        }
        EXPECT_TRUE(error == PathSearch::PATH_EXTENDED || error == PathSearch::PATH_CONTRACTED);
    }
    EXPECT_EQ(calls, 38);
    EXPECT_EQ(path.back().node, nodes[2][9]);

    // reset node states to enabled
    for (int i = 0; i < 3; ++i) {
        for (auto& node : nodes[i]) {
            node->state = Graph::Node::ENABLED;
        }
    }
}

TEST(path_search, single_passive_path_iterations) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    PathSearch path_search({"B"});
    // set all nodes as no parking execpt 1
    for (int i = 0; i < 3; ++i) {
        for (auto& node : nodes[i]) {
            node->state = Graph::Node::NO_PARKING;
        }
    }
    nodes[2][9]->state = Graph::Node::ENABLED;

    // find the only node that allows parking
    Path path = {{nodes[0][0]}};
    ASSERT_EQ(path_search.iterate(path, 400), PathSearch::SUCCESS);
    EXPECT_EQ(path.back().node, nodes[2][9]);

    // expect a second attempt to take less iterations via previously cached cost estimates
    path.resize(1);
    ASSERT_EQ(path_search.iterate(path, 20), PathSearch::SUCCESS);
    EXPECT_EQ(path.back().node, nodes[2][9]);

    // try from a different start location
    path = {{nodes[0][5]}};
    ASSERT_EQ(path_search.iterate(path, 100), PathSearch::SUCCESS);
    EXPECT_EQ(path.back().node, nodes[2][9]);

    // reset node states to enabled
    for (int i = 0; i < 3; ++i) {
        for (auto& node : nodes[i]) {
            node->state = Graph::Node::ENABLED;
        }
    }
}
