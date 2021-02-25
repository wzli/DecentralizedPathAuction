#pragma once

#include <map>
#include <string>
#include <vector>

namespace decentralized_path_auction {

class Auction {
public:
    enum Error {
        SUCCESS,
        BIDDER_EMPTY,
        BIDDER_NOT_FOUND,
        BIDDER_MISMATCH,
        PRICE_NOT_FOUND,
        PRICE_BELOW_START,
        PRICE_ALREADY_EXIST,
        DURATION_NEGATIVE,
    };

    struct Bid;
    using Bids = std::map<float, Bid>;

    Auction(float start_price = 0);
    ~Auction();

    // non-copyable and non-movable
    Auction(const Auction&) = delete;
    Auction& operator=(const Auction&) = delete;

    Error insertBid(const std::string& bidder, float price, float duration, Bid*& prev);
    Error removeBid(const std::string& bidder, float price);
    void clearBids(float start_price) { this->~Auction(), new (this) Auction(start_price); }

    const Bids& getBids() const { return _bids; }
    Bids::const_iterator getHigherBid(float price, const std::string& exclude_bidder = "") const;
    Bids::const_iterator getHighestBid(const std::string& exclude_bidder = "") const;

private:
    Bids _bids;
};

template <class T>
class DenseId {
public:
    DenseId()
            : id(_free_ids.back()) {
        _free_ids.pop_back();
        if (_free_ids.empty()) {
            _free_ids.push_back(id + 1);
        }
    }
    // copy constructor is a hack to allow aggregate construction
    // but copies have different ID since they must be unique for every instance
    DenseId(const DenseId&)
            : DenseId() {}
    DenseId& operator=(const DenseId&) = delete;
    ~DenseId() { _free_ids.push_back(id); }

    operator size_t() const { return id; }
    static size_t count() { return _free_ids.front(); }

    const size_t id;

private:
    inline static std::vector<size_t> _free_ids = {0};
};

struct Auction::Bid {
    std::string bidder;
    float duration = 0;
    // maintain unique id for every bid
    DenseId<Bid> id = {};
    // links to other bids
    Bid* prev = nullptr;
    Bid* next = nullptr;
    Bid* lower = nullptr;

    // recursive functions
    bool detectCycle(std::vector<bool>& visited, const std::string& exclude_bidder = "") const;
    float totalDuration() const { return duration + (prev ? prev->totalDuration() : 0); };
    const Auction::Bid& head() const { return prev ? prev->head() : *this; }
};

}  // namespace decentralized_path_auction
