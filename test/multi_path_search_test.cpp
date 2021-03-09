#include <decentralized_path_auction/path_search.hpp>
#include <decentralized_path_auction/path_sync.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

void make_pathway(Graph& graph, Nodes& pathway, Point2D a, Point2D b, size_t n, Node::State state = Node::DEFAULT);

bool save_graph(const Graph& graph, const char* file);

// 00-01-02-03-04-05-06-07-08-09
// |
// 10-11-12-13-14-15-16-17-18-19
// |
// 20-21-22-23-24-25-26-27-28-29
std::vector<Nodes> make_test_graph(Graph& graph);

void print_path(const Path& path) {
    for (auto& visit : path) {
        printf("{[%.2f %.2f], t: %.2f, d: %.2e, p: %.2f, b: %.2f c:%.2f}\r\n", visit.node->position.x(),
                visit.node->position.y(), visit.time_estimate, visit.duration, visit.price, visit.base_price,
                visit.cost_estimate);
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
    Nodes src_candidates;
    float fallback_cost;
    size_t path_id = 0;

    Agent(PathSearch::Config config, Nodes src, Nodes dst, float fb_cost = FLT_MAX, float dst_dur = FLT_MAX)
            : path_search(std::move(config))
            , path{{path_search.selectSource(src)}}
            , src_candidates(std::move(src))
            , fallback_cost(fb_cost) {
        path_search.reset(std::move(dst), dst_dur);
    }

    const std::string& id() { return path_search.editConfig().agent_id; }
};

void multi_iterate(std::vector<Agent>& agents, int rounds, size_t iterations, bool allow_block, bool print = false) {
    PathSync path_sync;
    while (--rounds > 0) {
        for (auto& agent : agents) {
            auto search_error = agent.path_search.iterate(agent.path, iterations, agent.fallback_cost);
            ASSERT_LE(search_error, PathSearch::ITERATIONS_REACHED);
            if (print) {
                printf("%s error %d\r\n", agent.id().c_str(), search_error);
                print_path(agent.path);
            }
            ASSERT_EQ(path_sync.updatePath(agent.id(), agent.path, agent.path_id++), PathSync::SUCCESS);
            if (std::all_of(agents.begin(), agents.end(), [&](Agent& a) {
                    auto error = std::get<0>(path_sync.checkWaitConditions(a.id()));
                    if (error == PathSync::SOURCE_NODE_OUTBID) {
                        a.path = {a.path_search.selectSource(a.src_candidates)};
                    }
                    return error == PathSync::SUCCESS ||
                           (error == PathSync::REMAINING_DURATION_INFINITE && allow_block);
                })) {
                rounds = 0;
                break;
            }
        }
    }
    if (print) {
        save_paths(path_sync, "paths.csv");
    }
    ASSERT_EQ(rounds, -1);
}

// TODO:
// test back and forth path wait dependencies

TEST(multi_path_search, head_on) {
    // input
    // A------------>A
    // 0-1-2-3-4-5-6-7-8-9
    //     B<------------B
    // expect both routes to be satisfied
    // and whoever plans last to have higher bids
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0]}, {nodes[7]}),
                Agent({"B"}, {nodes[9]}, {nodes[2]}),
        };
        multi_iterate(agents, 10, 100, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[7]);
        ASSERT_EQ(agents[1].path.back().node, nodes[2]);
        ASSERT_EQ(agents[0].path.back().node, agents[1].path[2].node);
        ASSERT_LT(agents[0].path.back().price, agents[1].path[2].price);
    }
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[9]}, {nodes[2]}),
                Agent({"A"}, {nodes[0]}, {nodes[7]}),
        };
        multi_iterate(agents, 10, 100, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[2]);
        ASSERT_EQ(agents[1].path.back().node, nodes[7]);
        ASSERT_EQ(agents[1].path.back().node, agents[0].path[2].node);
        ASSERT_GT(agents[1].path.back().price, agents[0].path[2].price);
    }
}

TEST(multi_path_search, head_on_cost_fallback) {
    // input
    //   A------------>A
    // 0-1-2-3-4-5-6-7-8-9
    //   B<------------B
    // expect (A wins via higher fallback cost)
    //   A------------>A
    // 0-1-2-3-4-5-6-7-8-9
    //                 B>B
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[1]}, {nodes[8]}, 200),
                Agent({"B"}, {nodes[8]}, {nodes[1]}, 100),
        };
        multi_iterate(agents, 10, 100, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[8]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[8]}, {nodes[1]}, 100),
                Agent({"A"}, {nodes[1]}, {nodes[8]}, 200),
        };
        multi_iterate(agents, 10, 100, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[8]);
    }
}

TEST(multi_path_search, head_on_desperate_fallback) {
    // input
    // A-------------->A
    // 0-1-2-3-4-5-6-7-8-9
    // B<------------B
    // expect (A wins via higher fallback desperation)
    // A-------------->A
    // 0-1-2-3-4-5-6-7-8-9
    //               B>B
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0]}, {nodes[7]}, 200),
                Agent({"B"}, {nodes[7]}, {nodes[0]}, 200),
        };
        multi_iterate(agents, 10, 100, false, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[7]);
        ASSERT_EQ(agents[1].path.back().node, nodes[8]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[7]}, {nodes[0]}, 200),
                Agent({"A"}, {nodes[0]}, {nodes[7]}, 200),
        };
        multi_iterate(agents, 10, 100, false, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[8]);
        ASSERT_EQ(agents[1].path.back().node, nodes[7]);
    }
}

TEST(multi_path_search, head_on_cost_limit) {
    // input
    // A---------------->A
    // 0-1-2-3-4-5-6-7-8-9
    // B<----------------B
    // expect (B gives up planning due to lower cost limit)
    // A---------------->A
    // 0-1-2-3-4-5-6-7-8-9
    //                   B
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A", 200}, {nodes[0]}, {nodes[9]}),
                Agent({"B", 100}, {nodes[9]}, {nodes[0]}),
        };
        multi_iterate(agents, 5, 100, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B", 100}, {nodes[9]}, {nodes[0]}),
                Agent({"A", 200}, {nodes[0]}, {nodes[9]}),
        };
        multi_iterate(agents, 5, 100, true);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
    }
}

TEST(multi_path_search, duplicate_requests) {
    // input
    // A---------------->A
    // 0-1-2-3-4-5-6-7-8-9
    //  B--------------->B
    // expect (B src pushed to 1)
    // A---------------->A
    // 0-1-2-3-4-5-6-7-8-9
    //   B-------------->B
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0]}, {nodes[9]}, FLT_MAX, 100),
                Agent({"B"}, {nodes[0], nodes[1]}, {nodes[9]}, FLT_MAX, 100),
        };
        multi_iterate(agents, 5, 400, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
        ASSERT_EQ(agents[0].path.front().node, nodes[0]);
        ASSERT_EQ(agents[1].path.front().node, nodes[1]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[0], nodes[1]}, {nodes[9]}, FLT_MAX, 100),
                Agent({"A"}, {nodes[0]}, {nodes[9]}, FLT_MAX, 100),
        };
        multi_iterate(agents, 5, 400, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
        ASSERT_EQ(agents[0].path.front().node, nodes[1]);
        ASSERT_EQ(agents[1].path.front().node, nodes[0]);
    }
}

TEST(multi_path_search, follow) {
    // input
    //   A-------------->A
    // 0-1-2-3-4-5-6-7-8-9
    // B-------------->B
    // expect B to follow behind A
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[1]}, {nodes[9]}),
                Agent({"B"}, {nodes[0]}, {nodes[8]}),
        };
        multi_iterate(agents, 5, 100, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[8]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[0]}, {nodes[8]}),
                Agent({"A"}, {nodes[1]}, {nodes[9]}),
        };
        multi_iterate(agents, 5, 100, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[8]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
    }
}

TEST(multi_path_search, dodge) {
    // input A and B start and both ends and swap places
    //                            A>
    // 00-01-02-03-04-05-06-07-08-09
    // |                          >B
    // |
    // 10-11-12-13-14-15-16-17-18-19
    // |
    // |                          >A
    // 20-21-22-23-24-25-26-27-28-29
    //                            B>
    // expect successful route with one of them dodging in the middle
    Graph graph;
    auto nodes = make_test_graph(graph);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0][9]}, {nodes[2][9]}),
                Agent({"B"}, {nodes[2][9]}, {nodes[0][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[2][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[0][9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[2][9]}, {nodes[0][9]}),
                Agent({"A"}, {nodes[0][9]}, {nodes[2][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[0][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[2][9]);
    }
}

TEST(multi_path_search, asymetric_dodge) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}),
                Agent({"B"}, {nodes[1][9]}, {nodes[0][5]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[0][5]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[1][9]}, {nodes[0][5]}),
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[1].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[0].path.back().node, nodes[0][5]);
    }
}

TEST(multi_path_search, wait) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0][0]}, {nodes[1][9]}),
                Agent({"B"}, {nodes[1][9]}, {nodes[2][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[2][9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[1][9]}, {nodes[2][9]}),
                Agent({"A"}, {nodes[0][0]}, {nodes[1][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[1].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[0].path.back().node, nodes[2][9]);
    }
}

TEST(multi_path_search, passive_push) {
    // input
    // A-------------->A
    // 0-1-2-3-4-5-6-7-8-9
    //  B
    // expect B get pushed by A
    // A-------------->A
    // 0-1-2-3-4-5-6-7-8-9
    //  B--------------->B
    Graph graph;
    Nodes nodes;
    make_pathway(graph, nodes, {0, 0}, {9, 0}, 10);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0]}, {nodes[8]}),
                Agent({"B"}, {nodes[0], nodes[1]}, {}),
        };
        multi_iterate(agents, 5, 400, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[8]);
        ASSERT_EQ(agents[1].path.back().node, nodes[9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[0], nodes[1]}, {}),
                Agent({"A"}, {nodes[0]}, {nodes[8]}),
        };
        multi_iterate(agents, 5, 400, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[8]);
    }
}

TEST(multi_path_search, passive_push_side) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[1][0]}, {nodes[1][8]}),
                Agent({"B"}, {nodes[1][5]}, {}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[1][8]);
        ASSERT_EQ(agents[1].path.back().node, nodes[1][9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[1][5]}, {}),
                Agent({"A"}, {nodes[1][0]}, {nodes[1][8]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[1].path.back().node, nodes[1][8]);
        ASSERT_EQ(agents[0].path.back().node, nodes[1][9]);
    }
}

TEST(multi_path_search, passive_push_out) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}),
                Agent({"B"}, {nodes[1][9]}, {}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[2][0]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[1][9]}, {}),
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[2][0]);
        ASSERT_EQ(agents[1].path.back().node, nodes[1][9]);
    }
}

TEST(multi_path_search, ailse_enter_avoid) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    {
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}),
                Agent({"B"}, {nodes[0][2]}, {nodes[0][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[0][9]);
    }
    // try again with B first to plan
    {
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[0][2]}, {nodes[0][9]}),
                Agent({"A"}, {nodes[0][5]}, {nodes[1][9]}),
        };
        multi_iterate(agents, 5, 500, false);
        ASSERT_EQ(agents[1].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[0].path.back().node, nodes[0][9]);
    }
}

TEST(multi_path_search, ailse_exit_block) {
    Graph graph;
    auto nodes = make_test_graph(graph);
    // A wants to exit ailse, but B blocks
    {
        // A yields to be due to lower fallback cost
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}, 200),
                Agent({"B"}, {nodes[0][8]}, {nodes[0][7]}, 400),
        };
        multi_iterate(agents, 10, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[0][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[0][7]);
    }
    {
        // repeat above with swapped order
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[0][8]}, {nodes[0][7]}, 400),
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}, 200),
        };
        multi_iterate(agents, 10, 500, false);
        ASSERT_EQ(agents[1].path.back().node, nodes[0][9]);
        ASSERT_EQ(agents[0].path.back().node, nodes[0][7]);
    }
    {
        // B yields to be due to lower fallback cost
        std::vector<Agent> agents = {
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}, 500),
                Agent({"B"}, {nodes[0][8]}, {nodes[0][7]}, 200),
        };
        multi_iterate(agents, 10, 500, false);
        ASSERT_EQ(agents[0].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[1].path.back().node, nodes[2][0]);
    }
    {
        // repeat above with swapped order
        std::vector<Agent> agents = {
                Agent({"B"}, {nodes[0][8]}, {nodes[0][7]}, 200),
                Agent({"A"}, {nodes[0][9]}, {nodes[1][9]}, 500),
        };
        multi_iterate(agents, 10, 500, false);
        ASSERT_EQ(agents[1].path.back().node, nodes[1][9]);
        ASSERT_EQ(agents[0].path.back().node, nodes[2][0]);
    }
}
