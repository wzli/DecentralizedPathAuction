#pragma once

#include <map>
#include <string>

namespace decentralized_path_auction {

class Auction {
public:
    struct Bid {
        std::string bidder;
        float price;
        float duration = 0;
        Bid* prev = nullptr;
        Bid* next = nullptr;
        Bid* higher = nullptr;
        std::map<float, Bid>::iterator self = {};
        // search cache
        mutable size_t search_id = 0;
        mutable size_t collision_id = 0;
        mutable float cost_estimate = 0;

        float totalDuration() const { return duration + (prev ? prev->totalDuration() : 0); };
    };

    using Bids = std::map<float, Bid>;

    Auction(float start_price)
            : _bids({{start_price, {"", start_price, 0}}}) {}

    bool insertBid(Bid bid, Bid*& prev);
    bool removeBid(const Bid& bid);

    const Bid& getHighestBid(const std::string& exclude_bidder = "") const;
    const Bids& getBids() const { return _bids; }
    float getStartPrice() const { return _bids.begin()->first; }

    static bool checkCollision(const Auction::Bid* bid, size_t collision_id, const std::string& exclude_bidder = "");

private:
    Bids _bids;
};

}  // namespace decentralized_path_auction
