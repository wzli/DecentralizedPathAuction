#pragma once

#include <map>
#include <string>

namespace decentralized_path_auction {

class Auction {
public:
    enum Error {
        SUCCESS,
        BIDDER_EMPTY,
        BIDDER_NOT_FOUND,
        BIDDER_MISMATCH,
        PRICE_NOT_FOUND,
        PRICE_BELOW_START,
        PRICE_ALREADY_EXIST,
        DURATION_NEGATIVE,
    };

    struct Bid {
        std::string bidder;
        float duration = 0;
        // links to other bids
        Bid* prev = nullptr;
        Bid* next = nullptr;
        Bid* higher = nullptr;
        // search cache
        mutable size_t cycle_nonce = 0;
        mutable size_t cost_nonce = 0;
        mutable float cost_estimate = 0;

        // recursive functions
        bool detectCycle(size_t nonce, const std::string& exclude_bidder = "") const;
        float totalDuration() const { return duration + (prev ? prev->totalDuration() : 0); };
    };

    using Bids = std::map<float, Bid>;

    Auction(float start_price)
            : _bids({{start_price, {""}}}) {}
    ~Auction();

    // non-copyable and non-movable
    Auction(const Auction&) = delete;
    Auction& operator=(const Auction&) = delete;

    Error insertBid(const std::string& bidder, float price, float duration, Bid*& prev);
    Error removeBid(const std::string& bidder, float price);
    void clearBids(float start_price) { this->~Auction(), new (this) Auction(start_price); }

    const Bids& getBids() const { return _bids; }
    float getStartPrice() const { return _bids.begin()->first; }
    Bids::const_iterator getHigherBid(float price, const std::string& exclude_bidder = "") const;
    Bids::const_iterator getHighestBid(const std::string& exclude_bidder = "") const;

private:
    Bids _bids;
};

}  // namespace decentralized_path_auction
