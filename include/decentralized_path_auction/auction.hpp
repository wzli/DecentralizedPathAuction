#pragma once

#include <map>
#include <memory>
#include <string>

namespace decentralized_path_auction {

class Auction {
public:
    struct Bid {
        std::string bidder;
        size_t index;
        float price;

        // compare operator
        bool operator<(const Bid& rhs) const {
            return price == rhs.price
                           ? (index == rhs.index ? bidder < rhs.bidder : index < rhs.index)
                           : price < rhs.price;
        }
    };

    struct UserData;
    using Bids = std::map<Bid, std::shared_ptr<UserData>>;

    Auction(float start_price)
            : _bids({{{"start_price", 0, start_price}, nullptr}}) {}

    bool insertBid(Bid bid) {
        // must be greater than start price
        if (bid.price <= getStartPrice()) {
            return false;
        }
        // insert only if same price bid doesn't already exist
        auto found = _bids.lower_bound({"", 0, bid.price});
        if (found != _bids.end() && found->first.price == bid.price) {
            return false;
        }
        return _bids.insert({std::move(bid), nullptr}).second;
    };

    bool removeBid(const Bid& bid) {
        // don't remove start price
        if (bid.price == getStartPrice()) {
            return false;
        }
        return _bids.erase(bid);
    };

    Bids& getBids() { return _bids; }
    const Bids& getBids() const { return _bids; }
    float getStartPrice() const { return _bids.begin()->first.price; }

    // TODO: if you occupy a node, you bid float max

private:
    Bids _bids;
};

}  // namespace decentralized_path_auction
