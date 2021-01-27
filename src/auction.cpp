#include <decentralized_path_auction/auction.hpp>

namespace decentralized_path_auction {

Auction::Error Auction::insertBid(const std::string& bidder, float price, float duration, Bid*& prev) {
    // must contain bidder name
    if (bidder.empty()) {
        return BIDDER_EMPTY;
    }
    // must be greater than start price
    if (price <= getStartPrice()) {
        return PRICE_BELOW_START;
    }
    // positive duration only
    if (duration < 0) {
        return DURATION_NEGATIVE;
    }
    // linked bids must be from the same bidder
    if (prev && prev->bidder != bidder) {
        return BIDDER_PREV_MISMATCH;
    }
    // insert bid
    auto [it, result] = _bids.insert({price, {bidder, duration}});
    // reject if same price bid already exists
    if (!result) {
        return PRICE_ALREADY_EXIST;
    }
    // update prev link
    if (prev) {
        prev->next = &it->second;
    }
    it->second.prev = prev;
    prev = &it->second;
    // store link to next highest bid
    if (std::next(it) != _bids.end()) {
        it->second.higher = &std::next(it)->second;
    }
    if (it != _bids.begin()) {
        std::prev(it)->second.higher = &it->second;
    }
    return SUCCESS;
}

Auction::Error Auction::removeBid(const std::string& bidder, float price) {
    // don't remove start price
    if (bidder.empty()) {
        return BIDDER_EMPTY;
    }
    // only delete when price and bidder matches
    auto found = _bids.find(price);
    if (found == _bids.end() || found->second.bidder != bidder) {
        return BIDDER_NOT_FOUND;
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
    return SUCCESS;
}

Auction::Bids::const_iterator Auction::getHighestBid(const std::string& exclude_bidder) const {
    auto highest_bid = std::prev(_bids.end());
    while (highest_bid != _bids.begin() && highest_bid->second.bidder == exclude_bidder) {
        --highest_bid;
    }
    return highest_bid;
}

bool Auction::Bid::detectCycle(size_t nonce, const std::string& exclude_bidder) const {
    // cycle occured if previously visited bid was visited again
    if (cycle_nonce == nonce) {
        return true;
    }
    // mark traversed bids as visited
    cycle_nonce = nonce;
    // detect cycle for next bids in time (first by auction, then by path)
    return (higher && higher->detectCycle(nonce, exclude_bidder)) ||
           // skip the current bid's path if the bidder is excluded
           (bidder != exclude_bidder && next && next->detectCycle(nonce, exclude_bidder));
}

}  // namespace decentralized_path_auction
