#pragma once

#include <map>
#include <string>

namespace decentralized_path_auction {

class Auction {
public:
    struct Bid {
        std::string bidder;
        float price;
        float travel_time = 0;
        Bid* prev = nullptr;
        Bid* next = nullptr;
        // search cache
        mutable size_t search_id = 0;
        mutable float cost_estimate = 0;
    };

    using Bids = std::map<float, Bid>;

    Auction(float start_price)
            : _bids({{start_price, {"", start_price}}}) {}

    bool insertBid(Bid bid, Bid*& prev);
    bool removeBid(const Bid& bid);

    bool checkCollision(float src_price, float dst_price, const Auction::Bids& dst_bids,
            const std::string& exclude_bidder = "") const;

    const Bids& getBids() const { return _bids; }
    float getStartPrice() const { return _bids.begin()->first; }

    // TODO: if you occupy a node, you bid float max

private:
    Bids _bids;
};

}  // namespace decentralized_path_auction
