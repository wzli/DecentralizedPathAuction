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

    bool insertBid(Bid bid, Bid*& prev) {
        // must contain bidder name
        if (bid.bidder.empty()) {
            return false;
        }
        // must be greater than start price
        if (bid.price <= getStartPrice()) {
            return false;
        }
        // positive travel time only
        if (bid.travel_time < 0) {
            return false;
        }
        // linked bids must be from the same bidder
        if (prev && prev->bidder != bid.bidder) {
            return false;
        }
        // insert bid
        auto [it, result] = _bids.insert({bid.price, std::move(bid)});
        // reject if same price bid already exists
        if (!result) {
            return false;
        }
        // update prev link
        it->second.prev = prev;
        if (prev) {
            prev->next = &it->second;
        }
        prev = &it->second;
        return true;
    };

    bool removeBid(const Bid& bid) {
        // don't remove start price
        if (bid.price == getStartPrice()) {
            return false;
        }
        auto found = _bids.find(bid.price);
        // only delete when price and bidder matches
        if (found == _bids.end() || found->second.bidder != bid.bidder) {
            return false;
        }
        // fix links after deletion
        if (found->second.next) {
            found->second.next->prev = found->second.prev;
        }
        if (found->second.prev) {
            found->second.prev->next = found->second.next;
        }
        _bids.erase(found);
        return true;
    };

    const Bids& getBids() const { return _bids; }
    float getStartPrice() const { return _bids.begin()->first; }

    // TODO: if you occupy a node, you bid float max

private:
    Bids _bids;
};

}  // namespace decentralized_path_auction
