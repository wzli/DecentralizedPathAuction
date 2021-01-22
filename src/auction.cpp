#include <decentralized_path_auction/auction.hpp>

namespace decentralized_path_auction {

bool Auction::insertBid(Bid bid, Bid*& prev) {
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
}

bool Auction::removeBid(const Bid& bid) {
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
}

bool Auction::checkCollision(float src_price, float dst_price, const Auction::Bids& dst_bids,
        const std::string& exclude_bidder) const {
    for (auto dst_bid = ++dst_bids.begin(); dst_bid != dst_bids.end(); ++dst_bid) {
        if (dst_bid->second.bidder == exclude_bidder) {
            continue;
        }
        const auto check_link = [&](const Auction::Bid* link) {
            auto src_bid = _bids.find(link->price);
            return src_bid != _bids.end() && &src_bid->second == link &&
                   ((src_bid->first > src_price) != (dst_bid->first > dst_price));
        };
        if ((dst_bid->second.prev && check_link(dst_bid->second.prev)) ||
                (dst_bid->second.next && check_link(dst_bid->second.next))) {
            return true;
        }
    }
    return false;
}

}  // namespace decentralized_path_auction
