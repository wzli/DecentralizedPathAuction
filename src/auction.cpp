#include <decentralized_path_auction/auction.hpp>

namespace decentralized_path_auction {

Auction::Auction(float start_price)
        : _bids({{start_price, {"", start_price, 0}}}) {
    _bids.begin()->second.self = _bids.begin();
}

bool Auction::insertBid(Bid bid, Bid*& prev) {
    // must contain bidder name
    if (bid.bidder.empty()) {
        return false;
    }
    // must be greater than start price
    if (bid.price <= getStartPrice()) {
        return false;
    }
    // positive duration only
    if (bid.duration < 0) {
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
    // store self iterator
    it->second.self = it;
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

bool Auction::checkCollision(
        float src_price, float dst_price, const Auction::Bids& dst_bids, const std::string& exclude_bidder) const {
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

bool Auction::checkCollision(const Auction::Bid* bid, size_t collision_id, const std::string& exclude_bidder) {
    // termination condition
    if (!bid || bid->bidder.empty()) {
        return false;
    }
    // skip to next bid in auction if bidder is excluded
    if (bid->bidder == exclude_bidder) {
        bid = &std::prev(bid->self)->second;
    }
    // collision occured if previously visited bid was visited again
    if (bid->collision_id == collision_id) {
        return true;
    }
    // mark traversed bids as visited
    bid->collision_id = collision_id;
    // check collision for next bids in time (first by auction, then by path)
    return checkCollision(&std::prev(bid->self)->second, collision_id) || checkCollision(bid->next, collision_id);
}

const Auction::Bid& Auction::getHighestBid(const std::string& exclude_bidder) const {
    for (auto bid = _bids.rbegin(); bid != _bids.rend(); ++bid) {
        if (bid->second.bidder != exclude_bidder) {
            return bid->second;
        }
    }
    return _bids.begin()->second;
}

}  // namespace decentralized_path_auction
