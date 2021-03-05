#include <decentralized_path_auction/auction.hpp>

namespace decentralized_path_auction {

Auction::Auction(float start_price)
        : _bids({{start_price, {""}}}) {}

Auction::~Auction() {
    for (auto& bid : _bids) {
        if (bid.second.next) {
            bid.second.next->prev = bid.second.prev;
        }
        if (bid.second.prev) {
            bid.second.prev->next = bid.second.next;
        }
    }
}

Auction::Error Auction::insertBid(const std::string& bidder, float price, float duration, Bid*& prev) {
    // must contain bidder name
    if (bidder.empty()) {
        return BIDDER_EMPTY;
    }
    // must be greater than start price
    if (price <= _bids.begin()->first) {
        return PRICE_BELOW_START;
    }
    // positive duration only
    if (duration < 0) {
        return DURATION_NEGATIVE;
    }
    // linked bids must be from the same bidder
    if (prev && prev->bidder != bidder) {
        return BIDDER_MISMATCH;
    }
    // insert bid
    auto [it, result] = _bids.insert({price, {bidder, duration}});
    // reject if same price bid already exists
    if (!result) {
        return PRICE_ALREADY_EXIST;
    }
    // update prev and next link
    if (prev) {
        if ((it->second.next = prev->next)) {
            prev->next->prev = &it->second;
        }
        prev->next = &it->second;
    }
    it->second.prev = prev;
    prev = &it->second;
    // update higher and lower link
    if (std::next(it) != _bids.end()) {
        std::next(it)->second.lower = &it->second;
        it->second.higher = &std::next(it)->second;
    }
    // start bid must exist
    assert(std::prev(it) != _bids.end());
    std::prev(it)->second.higher = &it->second;
    it->second.lower = &std::prev(it)->second;
    return SUCCESS;
}

Auction::Error Auction::removeBid(const std::string& bidder, float price) {
    // don't remove start price
    if (bidder.empty()) {
        return BIDDER_EMPTY;
    }
    // only delete when price and bidder matches
    auto found = _bids.find(price);
    if (found == _bids.end()) {
        return PRICE_NOT_FOUND;
    }
    auto& bid = found->second;
    if (bid.bidder != bidder) {
        return BIDDER_NOT_FOUND;
    }
    // fix links after deletion
    if (bid.next) {
        bid.next->prev = bid.prev;
    }
    if (bid.prev) {
        bid.prev->next = bid.next;
    }
    if (std::next(found) != _bids.end()) {
        std::next(found)->second.lower = bid.lower;
    }
    assert(std::prev(found) != _bids.end());
    std::prev(found)->second.higher = bid.higher;
    // erase bid
    _bids.erase(found);
    return SUCCESS;
}

Auction::Bids::const_iterator Auction::getHigherBid(float price, const std::string& exclude_bidder) const {
    auto bid = _bids.upper_bound(price);
    while (!exclude_bidder.empty() && bid != _bids.end() && bid->second.bidder == exclude_bidder) {
        ++bid;
    }
    return bid;
}

Auction::Bids::const_iterator Auction::getHighestBid(const std::string& exclude_bidder) const {
    auto bid = std::prev(_bids.end());
    while (!exclude_bidder.empty() && bid != _bids.begin() && bid->second.bidder == exclude_bidder) {
        --bid;
    }
    return bid;
}

bool Auction::Bid::detectCycle(std::vector<CycleVisit>& visits, size_t nonce, const std::string& exclude_bidder) const {
    assert(visits.size() >= DenseId<Bid>::count());
    // cycle occured if previously visited ancestor bid was visited again
    if (visits[id].nonce == nonce) {
        return visits[id].in_cycle;
    }
    // mark traversed bids as visited
    visits[id].nonce = nonce;
    visits[id].in_cycle = 1;
    // detect cycle for next bids in time (first by auction rank, then by path sequence)
    return (visits[id].in_cycle =
                    // detect cycle at next lower bid
            (lower && lower->detectCycle(visits, nonce, exclude_bidder)) ||
            // skip the current bid's path if the bidder is excluded
            (bidder != exclude_bidder &&
                    // detect cycle at next lower bid of previous bid
                    ((prev && prev->lower && prev->lower->detectCycle(visits, nonce, exclude_bidder)) ||
                            // clear flag on next bid for temporary bids inbetween existing bids
                            (next && ((visits[next->id].nonce == nonce && visits[next->id].in_cycle & 2 &&
                                              --visits[next->id].nonce),
                                             // detect cycle at next bid
                                             next->detectCycle(visits, nonce, exclude_bidder))))));
}

float Auction::Bid::waitDuration(const std::string& exclude_bidder) const {
    thread_local std::vector<bool> visits;
    visits.resize(DenseId<Bid>::count());
    if (visits[id]) {
        return std::numeric_limits<float>::max();
    }
    visits[id] = true;
    float higher_wait_duration = higher ? higher->waitDuration(exclude_bidder) : 0;
    float prev_wait_duration =
            bidder == exclude_bidder ? 0 : (duration + (prev ? prev->waitDuration(exclude_bidder) : 0));
    visits[id] = false;
    return std::max(higher_wait_duration, prev_wait_duration);
}

}  // namespace decentralized_path_auction
