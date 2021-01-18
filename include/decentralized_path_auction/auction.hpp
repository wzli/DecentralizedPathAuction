#pragma once

#include <map>
#include <memory>
#include <string>

namespace decentralized_path_auction {

class Auction {
public:
    struct UserData;

    struct Bidder {
        std::string name;
        size_t index;
        mutable std::shared_ptr<UserData> user_data = nullptr;

        bool operator==(const Bidder& rhs) const { return name == rhs.name && index == rhs.index; }
        bool operator<(const Bidder& rhs) const {
            return index == rhs.index ? name < rhs.name : index < rhs.index;
        }
    };

    using Bid = std::pair<float, Bidder>;
    using Bids = std::map<float, Bidder>;

    Auction(float start_price)
            : _bids({{start_price, {"start_price", 0}}}) {}

    bool insertBid(Bid bid) {
        // must be greater than start price
        if (bid.first <= getStartPrice()) {
            return false;
        }
        // insert only if same price bid doesn't already exist
        auto result = _bids.emplace(std::move(bid));
        _event_count += result.second;
        return result.second;
    };

    bool removeBid(const Bid& bid) {
        // don't remove start price
        if (bid.first == getStartPrice()) {
            return false;
        }
        auto found = _bids.find(bid.first);
        if (found == _bids.end() || !(found->second == bid.second)) {
            return false;
        }
        _bids.erase(found);
        ++_event_count;
        return true;
    };

    const Bids& getBids() const { return _bids; }
    float getStartPrice() const { return _bids.begin()->first; }

    // TODO: below
    // gotta write unit tests for this file
    // if you occupy a node, you bid float max
    void shiftBids();

private:
    Bids _bids;
    size_t _event_count;
};

}  // namespace decentralized_path_auction
