#include <decentralized_path_auction/path_search.hpp>
#include <decentralized_path_auction/path_sync.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Nodes& pathway, Point2D a, Point2D b, size_t n, Node::State state = Node::DEFAULT);

bool save_graph(const Graph& graph, const char* file);

// see single path search test for graph definition
std::vector<Nodes> make_test_graph(Graph& graph);

void print_path(const Path& path) {
    for (auto& visit : path) {
        printf("{[%.2f %.2f], t: %.2f, p: %.2f, b: %.2f c:%.2f}\r\n", visit.node->position.x(),
                visit.node->position.y(), visit.time, visit.price, visit.base_price, visit.cost_estimate);
    }
    puts("");
}

bool save_paths(const PathSync& path_sync, const char* file) {
    auto fp = fopen(file, "w");
    if (!fp) {
        return false;
    }
    fprintf(fp, "agent_id, agent_idx, pos_x, pos_y, price, rank\r\n");
    int i = 0;
    for (auto& info : path_sync.getPaths()) {
        for (auto& visit : info.second.path) {
            auto& bids = visit.node->auction.getBids();
            fprintf(fp, "\"%s\", %d, %f, %f, %f, %lu\r\n", info.first.c_str(), i, visit.node->position.x(),
                    visit.node->position.y(), visit.price, std::distance(bids.begin(), bids.find(visit.price)));
        }
        ++i;
    }
    return !fclose(fp);
}

struct Agent {
    PathSearch path_search;
    Path path;
    size_t path_id = 0;

    Agent(PathSearch::Config config, const NodePtr& src, Nodes dst)
            : path_search(std::move(config))
            , path{{src}} {
        path_search.reset(std::move(dst));
    }

    const std::string& id() { return path_search.editConfig().agent_id; }
};

static void multi_iterate(std::vector<Agent>& agents, int rounds, size_t iterations, bool print = false) {
    PathSync path_sync;
    while (--rounds > 0) {
        for (auto& agent : agents) {
            auto search_error = agent.path_search.iterateFallback(agent.path, iterations);
            ASSERT_LE(search_error, PathSearch::COST_LIMIT_EXCEEDED);
            if (print) {
                printf("%s error %d\r\n", agent.id().c_str(), search_error);
                print_path(agent.path);
            }
            ASSERT_EQ(path_sync.updatePath(agent.id(), agent.path, agent.path_id++), PathSync::SUCCESS);
            EXPECT_FALSE(path_sync.detectCycle());
            if (std::all_of(agents.begin(), agents.end(), [&path_sync](Agent& a) {
                    Path segment;
                    return PathSync::SUCCESS == path_sync.getEntitledSegment(a.id(), segment);
                })) {
                rounds = 0;
                break;
            }
        }
    }
    ASSERT_EQ(rounds, -1);
}

// TODO:
// test evading into aile
// test back and forth path wait dependencies

TEST(multi_path_search, head_on) {
    Graph graph;
    Nodes nodes;  // A------------>A
    // 0-1-2-3-4-5-6-7-8-9
    //     B<------------B
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
    EXPECT_FALSE(path_sync.detectCycle());
    // print_path(path_a);

    ASSERT_EQ(path_search_b.iterate(path_b, 100), PathSearch::SUCCESS);
    ASSERT_EQ(path_b.back().node, nodes[2]);
    ASSERT_EQ(path_sync.updatePath("B", path_b, path_id_b++), PathSync::SUCCESS);
    EXPECT_FALSE(path_sync.detectCycle());
    // print_path(path_b);

    // test another round of path bids
    ASSERT_EQ(path_search_a.reset({nodes[7]}), PathSearch::SUCCESS);
    ASSERT_EQ(path_search_a.iterate(path_a, 100), PathSearch::SUCCESS);
    ASSERT_EQ(path_a.back().node, nodes[7]);
    ASSERT_EQ(path_sync.updatePath("A", path_a, path_id_a++), PathSync::SUCCESS);
    EXPECT_FALSE(path_sync.detectCycle());
    // print_path(path_a);

    ASSERT_EQ(path_search_b.reset({nodes[2]}), PathSearch::SUCCESS);
    ASSERT_EQ(path_search_b.iterate(path_b, 100), PathSearch::SUCCESS);
    ASSERT_EQ(path_b.back().node, nodes[2]);
    ASSERT_EQ(path_sync.updatePath("B", path_b, path_id_b++), PathSync::SUCCESS);
    EXPECT_FALSE(path_sync.detectCycle());
    // print_path(path_b);

    save_paths(path_sync, "head_on.csv");
}

TEST(multi_path_search, unavoidable_collision0) {
    // input
    //   A------------>A
    // 0-1-2-3-4-5-6-7-8-9
    //   B<------------B
    // expect (A wins via higher cost limit)
    //   A------------>A
    // 0-1-2-3-4-5-6-7-8-9
    //                 B>B
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A", 200}, nodes[1], {nodes[8]}),
                Agent({"B", 100}, nodes[8], {nodes[1]}),
        };
        multi_iterate(agents, 10, 100, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[8]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B", 100}, nodes[8], {nodes[1]}),
                Agent({"A", 200}, nodes[1], {nodes[8]}),
        };
        multi_iterate(agents, 10, 100, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[8]);
    }
}

TEST(multi_path_search, unavoidable_collision1) {
    // input
    // A-------------->A
    // 0-1-2-3-4-5-6-7-8-9
    // B<----------------B
    // expect (A wins via higher desperation)
    // A-------------->A
    // 0-1-2-3-4-5-6-7-8-9
    //                 B>B
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A", 200}, nodes[0], {nodes[8]}),
                Agent({"B", 200}, nodes[8], {nodes[0]}),
        };
        multi_iterate(agents, 10, 100, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[8]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B", 200}, nodes[8], {nodes[0]}),
                Agent({"A", 200}, nodes[0], {nodes[8]}),
        };
        multi_iterate(agents, 10, 100, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[8]);
    }
}
#if 0
#endif

TEST(multi_path_search, follow) {
    Graph graph;
    Nodes nodes;
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

    EXPECT_FALSE(path_sync.detectCycle());
    save_paths(path_sync, "follow.csv");
}

TEST(multi_path_search, push) {
    Graph graph;
    Nodes nodes;
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

    EXPECT_FALSE(path_sync.detectCycle());
    save_paths(path_sync, "push.csv");
}

TEST(multi_path_search, dodge) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    PathSync path_sync;
    size_t path_id_a = 0;
    size_t path_id_b = 0;
    PathSearch path_search_a({"A"});
    PathSearch path_search_b({"B"});
    Path path_a = {{nodes[0][9]}};
    Path path_b = {{nodes[2][9]}};
    ASSERT_EQ(path_search_a.reset({nodes[2][9]}), PathSearch::SUCCESS);
    ASSERT_EQ(path_search_b.reset({nodes[0][9]}), PathSearch::SUCCESS);

    EXPECT_EQ(path_search_a.iterate(path_a, 400), PathSearch::SUCCESS);
    // print_path(path_a);
    ASSERT_EQ(path_a.back().node, nodes[2][9]);
    ASSERT_EQ(path_sync.updatePath("A", path_a, path_id_a++), PathSync::SUCCESS);

    ASSERT_EQ(path_search_b.iterate(path_b, 400), PathSearch::SUCCESS);
    // print_path(path_b);
    ASSERT_EQ(path_b.back().node, nodes[0][9]);
    ASSERT_EQ(path_sync.updatePath("B", path_b, path_id_b++), PathSync::SUCCESS);

    EXPECT_FALSE(path_sync.detectCycle());

    save_paths(path_sync, "dodge.csv");
    save_graph(graph, "dodge_graph.csv");
}
