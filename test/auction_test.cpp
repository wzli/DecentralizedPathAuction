#include <decentralized_path_auction/auction.hpp>
#include <gtest/gtest.h>

using namespace decentralized_path_auction;

static void check_auction_links(const Auction::Bids& bids) {
    EXPECT_EQ(bids.rbegin()->second.next, nullptr);
    EXPECT_EQ(bids.begin()->second.lower, nullptr);
    EXPECT_EQ(bids.begin()->second.next, nullptr);
    EXPECT_EQ(bids.begin()->second.prev, nullptr);
    int i = 0;
    for (auto bid = &std::next(bids.begin())->second; bid; bid = bid->next, ++i) {
        if (bid->prev) {
            EXPECT_EQ(bid->prev->next, bid);
        } else {
            EXPECT_EQ(bid, &std::next(bids.begin())->second);
        }
        if (bid->next) {
            EXPECT_EQ(bid->next->prev, bid);
        } else {
            EXPECT_EQ(bid, &bids.rbegin()->second);
        }
        ASSERT_TRUE(bid->lower);
        EXPECT_EQ(bid->lower->next, bid->lower == &bids.begin()->second ? nullptr : bid);
    }
    EXPECT_EQ(i, bids.size() - 1);
}

TEST(auction, constructor) {
    Auction auction(10);
    EXPECT_EQ(auction.getBids().begin()->first, 10);
    EXPECT_EQ(auction.getBids().size(), 1);
    auto& [start_price, start_bid] = *auction.getBids().begin();
    EXPECT_EQ(start_price, 10);
    EXPECT_EQ(start_bid.bidder, "");
    EXPECT_EQ(start_bid.prev, nullptr);
    EXPECT_EQ(start_bid.next, nullptr);
    EXPECT_EQ(start_bid.lower, nullptr);
}

TEST(auction, destructor) {
    Auction auc1(0);
    {
        Auction a3(0);
        {
            Auction auc2(0);
            for (int i = 1; i <= 5; ++i) {
                Auction::Bid* prev = nullptr;
                EXPECT_EQ(auc1.insertBid("A", i, 0, prev), Auction::SUCCESS);
                EXPECT_EQ(auc2.insertBid("A", i, 0, prev), Auction::SUCCESS);
                EXPECT_EQ(a3.insertBid("A", i, 0, prev), Auction::SUCCESS);
            }
        }
        for (auto bid = std::next(auc1.getBids().begin()); bid != auc1.getBids().end(); ++bid) {
            EXPECT_EQ(bid->second.prev, nullptr);
            EXPECT_EQ(bid->second.next->next, nullptr);
            EXPECT_EQ(bid->second.next->prev, &bid->second);
        }
    }
    for (auto bid = std::next(auc1.getBids().begin()); bid != auc1.getBids().end(); ++bid) {
        EXPECT_EQ(bid->second.prev, nullptr);
        EXPECT_EQ(bid->second.next, nullptr);
    }
    EXPECT_EQ(auc1.getBids().size(), 6);
}

TEST(auction, insert_bid) {
    Auction auction(0);
    auto& bids = auction.getBids();
    Auction::Bid* prev = nullptr;
    // trivial input checks
    EXPECT_EQ(auction.insertBid("", 0, 0, prev), Auction::BIDDER_EMPTY);
    EXPECT_EQ(auction.insertBid("A", 0, 0, prev), Auction::PRICE_BELOW_START);
    EXPECT_EQ(auction.insertBid("A", -1, 0, prev), Auction::PRICE_BELOW_START);
    EXPECT_EQ(auction.insertBid("A", 1, -1, prev), Auction::DURATION_NEGATIVE);
    // add first bid
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev), Auction::SUCCESS);
    EXPECT_EQ(bids.begin()->second.lower, nullptr);
    EXPECT_EQ(prev->prev, nullptr);
    EXPECT_EQ(prev->next, nullptr);
    EXPECT_EQ(prev->lower, &bids.begin()->second);
    // more rejection checks
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev), Auction::PRICE_ALREADY_EXIST);
    EXPECT_EQ(auction.insertBid("B", 2, 0, prev), Auction::BIDDER_MISMATCH);
    // add chained bid
    for (int i = 2; i < 10; ++i) {
        EXPECT_EQ(auction.insertBid("A", i, 0, prev), Auction::SUCCESS);
    }
    EXPECT_EQ(prev->next, nullptr);
    prev = prev->prev;
    EXPECT_EQ(auction.insertBid("A", 8.5f, 0, prev), Auction::SUCCESS);
    check_auction_links(bids);
}

TEST(auction, remove_bid) {
    Auction auction(0);
    Auction::Bid* prev = nullptr;
    for (int i = 1; i <= 4; ++i) {
        EXPECT_EQ(auction.insertBid("A", i, 0, prev), Auction::SUCCESS);
    }
    EXPECT_EQ(auction.removeBid("", 1), Auction::BIDDER_EMPTY);
    EXPECT_EQ(auction.removeBid("A", 5), Auction::PRICE_NOT_FOUND);
    EXPECT_EQ(auction.removeBid("B", 1), Auction::BIDDER_NOT_FOUND);
    // remove middle bid
    auto& bids = auction.getBids();
    EXPECT_EQ(auction.removeBid("A", 3), Auction::SUCCESS);
    check_auction_links(bids);
    // remove last bid
    EXPECT_EQ(auction.removeBid("A", 4), Auction::SUCCESS);
    check_auction_links(bids);
    // remove first bid
    EXPECT_EQ(auction.removeBid("A", 1), Auction::SUCCESS);
    check_auction_links(bids);
}

TEST(auction, get_higher_bid) {
    Auction auction(0);
    EXPECT_EQ(auction.getHigherBid(0), auction.getBids().end());
    EXPECT_EQ(auction.getHigherBid(-1), auction.getBids().begin());
    Auction::Bid* prev_a = nullptr;
    Auction::Bid* prev_b = nullptr;
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
    EXPECT_EQ(auction.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
    EXPECT_EQ(auction.insertBid("A", 3, 0, prev_a), Auction::SUCCESS);
    EXPECT_EQ(auction.insertBid("B", 4, 0, prev_b), Auction::SUCCESS);
    for (int i = -1; i <= 3; ++i) {
        EXPECT_EQ(auction.getHigherBid(i)->first, i + 1);
    }
    EXPECT_EQ(auction.getHigherBid(4), auction.getBids().end());
    EXPECT_EQ(auction.getHigherBid(0, "A")->first, 2);
    EXPECT_EQ(auction.getHigherBid(0, "B")->first, 1);
    EXPECT_EQ(auction.getHigherBid(3, "A")->first, 4);
    EXPECT_EQ(auction.getHigherBid(3, "B"), auction.getBids().end());
}

TEST(auction, get_highest_bid) {
    Auction auction(0);
    EXPECT_EQ(auction.getHighestBid()->first, 0);
    EXPECT_EQ(auction.getHighestBid("A")->first, 0);
    Auction::Bid* prev_a = nullptr;
    Auction::Bid* prev_b = nullptr;
    EXPECT_EQ(auction.insertBid("A", 1, 0, prev_a), Auction::SUCCESS);
    EXPECT_EQ(auction.getHighestBid()->first, 1);
    EXPECT_EQ(auction.getHighestBid("A")->first, 0);
    EXPECT_EQ(auction.insertBid("B", 2, 0, prev_b), Auction::SUCCESS);
    EXPECT_EQ(auction.getHighestBid()->first, 2);
    EXPECT_EQ(auction.getHighestBid("A")->first, 2);
    EXPECT_EQ(auction.getHighestBid("B")->first, 1);
    EXPECT_EQ(auction.insertBid("B", 3, 0, prev_b), Auction::SUCCESS);
    EXPECT_EQ(auction.getHighestBid()->first, 3);
    EXPECT_EQ(auction.getHighestBid("A")->first, 3);
    EXPECT_EQ(auction.getHighestBid("B")->first, 1);
}
