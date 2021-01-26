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
    // store link to next highest bid
    auto higher = std::next(it);
    it->second.higher = higher == _bids.end() ? nullptr : &higher->second;
    if (it != _bids.begin()) {
        std::prev(it)->second.higher = &it->second;
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
    std::prev(found)->second.higher = found->second.higher;
    _bids.erase(found);
    return true;
}

const Auction::Bid& Auction::getHighestBid(const std::string& exclude_bidder) const {
    for (auto bid = _bids.rbegin(); bid != _bids.rend(); ++bid) {
        if (bid->second.bidder != exclude_bidder) {
            return bid->second;
        }
    }
    return _bids.begin()->second;
}

bool Auction::checkCollision(const Auction::Bid* bid, size_t collision_id, const std::string& exclude_bidder) {
    // termination condition
    if (!bid) {
        return false;
    }
    // collision occured if previously visited bid was visited again
    if (bid->collision_id == collision_id) {
        return true;
    }
    // mark traversed bids as visited
    bid->collision_id = collision_id;
    // check collision for next bids in time (first by auction, then by path)
    return checkCollision(bid->higher, collision_id, exclude_bidder) ||
           // skip the current bid's path if the bidder is excluded
           (bid->bidder != exclude_bidder && checkCollision(bid->next, collision_id, exclude_bidder));
}

}  // namespace decentralized_path_auction
