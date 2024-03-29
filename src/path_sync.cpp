#include <decentralized_path_auction/path_sync.hpp>

namespace decentralized_path_auction {

static const Auction::Bid* insertBids(const std::string& agent_id, Path::const_iterator it, Path::const_iterator end) {
    Auction::Bid* prev_bid = nullptr;
    for (; it != end; ++it) {
        if (it->node->auction.insertBid(agent_id, it->price, it->duration, prev_bid)) {
            return nullptr;
        };
    }
    return prev_bid;
}

static PathSync::Error removeBids(const std::string& agent_id, Path::const_iterator it, Path::const_iterator end) {
    bool error = false;
    for (; it != end; ++it) {
        error |= it->node->auction.removeBid(agent_id, it->price);
    }
    return error ? PathSync::VISIT_BID_ALREADY_REMOVED : PathSync::SUCCESS;
}

PathSync::Error PathSync::updatePath(const std::string& agent_id, const Path& path, size_t path_id) {
    // input checks
    if (agent_id.empty()) {
        return AGENT_ID_EMPTY;
    }
    // check path id
    if (auto found = _paths.find(agent_id);
            found != _paths.end() && path_id <= found->second.path_id && !found->second.path.empty()) {
        return PATH_ID_STALE;
    }
    if (auto path_error = validate(path)) {
        return path_error;
    }
    if (path.front().node->auction.getHighestBid(agent_id)->first > path.front().price) {
        return SOURCE_NODE_OUTBID;
    }
    // check if bid price is already taken by some other agent
    for (auto& visit : path) {
        auto& bids = visit.node->auction.getBids();
        auto found = bids.find(visit.price);
        if (found != bids.end() && found->second.bidder != agent_id) {
            return VISIT_PRICE_ALREADY_EXIST;
        }
    }
    // remove old bids and insert new ones
    auto& info = _paths[agent_id];
    auto remove_error = removeBids(agent_id, info.path.begin() + info.progress_min, info.path.end());
    auto tail_bid = insertBids(agent_id, path.begin(), path.end());
    assert(tail_bid && "insert bid failed");
    // check if new path causes cycle
    thread_local size_t cycle_nonce = 0;
    thread_local std::vector<CycleVisit> cycle_visits;
    cycle_visits.resize(DenseId<Auction::Bid>::count());
    if (tail_bid->head().detectCycle(cycle_visits, ++cycle_nonce)) {
        // revert bids back to previous path
        removeBids(agent_id, path.begin(), path.end());
        insertBids(agent_id, info.path.begin() + info.progress_min, info.path.end());
        // remove path entry if it's empty
        if (info.path.empty()) {
            _paths.erase(agent_id);
        }
        return PATH_CAUSES_CYCLE;
    }
    // update path
    info = {path, path_id, 0, 0};
    return remove_error;
}

PathSync::Error PathSync::updateProgress(
        const std::string& agent_id, size_t progress_min, size_t progress_max, size_t path_id) {
    // input checks
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return AGENT_ID_NOT_FOUND;
    }

    auto& info = found->second;
    if (path_id != info.path_id) {
        return PATH_ID_MISMATCH;
    }

    if (progress_min >= info.path.size()) {
        return PROGRESS_EXCEED_PATH_SIZE;
    }

    if (progress_max >= info.path.size()) {
        return PROGRESS_EXCEED_PATH_SIZE;
    }

    if (progress_max < progress_min) {
        return PROGRESS_MIN_EXCEED_MAX;
    }

    if (progress_min < info.progress_min) {
        return PROGRESS_DECREASE_DENIED;
    }

    if (progress_max < info.progress_max) {
        return PROGRESS_DECREASE_DENIED;
    }

    // remove bids upto the new progress
    if (auto error = removeBids(agent_id, info.path.begin() + info.progress_min, info.path.begin() + progress_min)) {
        return error;
    }
    info.progress_min = progress_min;
    info.progress_max = std::max(info.progress_max, progress_min);

    // don't claim nodes unless path has progressed
    // inorder to allow SOURCE_NODE_OUTBID to prompt user to query fallback path before that point
    if (progress_max == progress_min) {
        return SUCCESS;
    }

    // claim all nodes up to progress_max
    for (; info.progress_max < std::min(progress_max + 1, info.path.size()); ++info.progress_max) {
        auto& path = info.path[info.progress_max];
        auto& auction = path.node->auction;
        auto highest = auction.getHighestBid();
        // claim until agent is no longer highest bidder
        if (highest->second.bidder != agent_id) {
            break;
        }
        // skip already claimed nodes
        if (highest->first >= FLT_MAX) {
            continue;
        }
        // claimed nodes with price FLT_MAX
        if (auction.changeBid(highest->first, FLT_MAX)) {
            assert(!"could not change bid");
        }
        path.price = FLT_MAX;
    }
    --info.progress_max;
    assert(info.progress_max < info.path.size());
    // warn if desired progress_max couldn't be claimed
    if (info.progress_max < progress_max) {
        return PROGRESS_RANGE_CONFLICT;
    }
    return SUCCESS;
}

PathSync::Error PathSync::removePath(const std::string& agent_id) {
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return AGENT_ID_NOT_FOUND;
    }
    auto error =
            removeBids(agent_id, found->second.path.begin() + found->second.progress_min, found->second.path.end());
    _paths.erase(found);
    return error;
}

PathSync::Error PathSync::clearPaths() {
    int error = SUCCESS;
    for (auto& [agent_id, info] : _paths) {
        error |= removeBids(agent_id, info.path.begin() + info.progress_min, info.path.end());
    }
    _paths.clear();
    return static_cast<Error>(error);
}

PathSync::WaitStatus PathSync::checkWaitStatus(const std::string& agent_id) const {
    // find path
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return {AGENT_ID_NOT_FOUND, 0, FLT_MAX};
    }
    // validate path
    auto& info = found->second;
    assert(info.progress_min < info.path.size());
    for (size_t progress = info.progress_min; progress < info.path.size(); ++progress) {
        auto& visit = info.path[progress];
        switch (visit.node->state) {
            case Node::DELETED:
                return {VISIT_NODE_INVALID, progress, FLT_MAX};
            case Node::DISABLED:
                return {VISIT_NODE_DISABLED, progress, FLT_MAX};
            case Node::NO_PARKING:
                if (progress + 1 == info.path.size()) {
                    return {DESTINATION_NODE_NO_PARKING, progress, FLT_MAX};
                }
                // fallthrough
            default:
                auto& bids = visit.node->auction.getBids();
                auto bid = bids.find(visit.price);
                if (bid == bids.end() || bid->second.bidder != agent_id) {
                    return {VISIT_BID_ALREADY_REMOVED, progress, FLT_MAX};
                }
        }
    }
    // calculate remaining duration
    auto& last_bid = info.path.back().node->auction.getBids().find(info.path.back().price)->second;
    float prev_wait_duration = last_bid.prev ? last_bid.prev->waitDuration() : 0;
    float higher_wait_duration = last_bid.higher ? last_bid.higher->waitDuration() : 0;
    float remaining_duration = std::max(prev_wait_duration, higher_wait_duration);
    // calculate blocked progress
    size_t progress = info.progress_min;
    while (progress < info.path.size() &&
            info.path[progress].node->auction.getHighestBid()->second.bidder == agent_id) {
        // consider any path with progress_min == progress_max as blocked at that node
        auto& bids = info.path[progress].node->auction.getBids();
        if (progress > info.progress_min && std::any_of(std::next(bids.begin()), bids.end(), [&](const auto& bid) {
                auto& other_info = _paths.at(bid.second.bidder);
                return agent_id != bid.second.bidder && other_info.progress_min == other_info.progress_max &&
                       info.path[progress].node == other_info.path[other_info.progress_min].node;
            })) {
            break;
        }
        ++progress;
    }
    return {progress == info.progress_min   ? SOURCE_NODE_OUTBID
            : remaining_duration >= FLT_MAX ? REMAINING_DURATION_INFINITE
                                            : SUCCESS,
            progress, remaining_duration};
}

PathSync::Error PathSync::validate(const Visit& visit) const {
    if (!Node::validate(visit.node)) {
        return VISIT_NODE_INVALID;
    }
    if (visit.node->state == Node::DISABLED) {
        return VISIT_NODE_DISABLED;
    }
    if (visit.duration < 0) {
        return VISIT_DURATION_NEGATIVE;
    }
    if (visit.price < visit.node->auction.getBids().begin()->first) {
        return VISIT_PRICE_LESS_THAN_START_PRICE;
    }
    return SUCCESS;
}

PathSync::Error PathSync::validate(const Path& path) const {
    if (path.empty()) {
        return PATH_EMPTY;
    }
    for (auto& visit : path) {
        if (auto error = validate(visit)) {
            return error;
        }
    }
    if (path.back().node->state >= Node::NO_PARKING) {
        return DESTINATION_NODE_NO_PARKING;
    }
    // check for duplicate visits
    std::vector<std::pair<const Node*, float>> unique_buf;
    unique_buf.reserve(path.size());
    for (auto& visit : path) {
        unique_buf.emplace_back(visit.node.get(), visit.price);
    }
    std::sort(unique_buf.begin(), unique_buf.end());
    if (std::adjacent_find(unique_buf.begin(), unique_buf.end()) != unique_buf.end()) {
        return PATH_VISIT_DUPLICATED;
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
