#include <decentralized_path_auction/auction.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

TEST(bid_chain, dense_id) {
    struct T {};
    std::vector<DenseId<T>> ids(200);
    for (size_t i = 0; i < ids.size(); ++i) {
        ASSERT_EQ(i, ids[i]);
    }
    ids.resize(50);
    for (size_t i = 0; i < ids.size(); ++i) {
        ASSERT_EQ(i, ids[i]);
    }
    ids.resize(100);
    for (size_t i = 0; i < ids.size(); ++i) {
        ASSERT_EQ(i, ids[i]);
    }
}

TEST(bid_chain, total_duration) {
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

TEST(bid_chain, detect_cycle) {
    std::vector<CycleVisit> visited;
    size_t cycle_nonce = 0;
    {
        Auction auction(0);
        Auction::Bid* prev = nullptr;
        // single bid should not have any cycles
        EXPECT_EQ(auction.insertBid("A", 1, 0, prev), Auction::SUCCESS);
        EXPECT_FALSE(prev->detectCycle(visited, ++cycle_nonce));

        // two consecutive bids in the same auction with inversed order causes cycle
        EXPECT_EQ(auction.insertBid("A", 2, 0, prev), Auction::SUCCESS);
        ASSERT_TRUE(prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev->prev->detectCycle(visited, ++cycle_nonce));

        // remove culptrit bid
        prev = prev->prev;
        EXPECT_EQ(auction.removeBid("A", 2), Auction::SUCCESS);
        EXPECT_FALSE(prev->detectCycle(visited, ++cycle_nonce));

        // two consecutive bids in the same auction without order inversion still causes cycle
        EXPECT_EQ(auction.insertBid("A", 0.5f, 0, prev), Auction::SUCCESS);
        EXPECT_TRUE(prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev->prev->detectCycle(visited, ++cycle_nonce));
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
        EXPECT_FALSE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
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
        EXPECT_FALSE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
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
        EXPECT_TRUE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
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

        EXPECT_TRUE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
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

        EXPECT_TRUE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
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

        EXPECT_FALSE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(visited, ++cycle_nonce));
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

        EXPECT_FALSE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(visited, ++cycle_nonce));
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

        EXPECT_FALSE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(visited, ++cycle_nonce));
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

        EXPECT_FALSE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_a->prev->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_FALSE(prev_b->prev->prev->detectCycle(visited, ++cycle_nonce));
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

        EXPECT_TRUE(prev_a->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_a->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_a->prev->prev->detectCycle(visited, ++cycle_nonce));
        EXPECT_TRUE(prev_b->prev->prev->detectCycle(visited, ++cycle_nonce));
    }
}
