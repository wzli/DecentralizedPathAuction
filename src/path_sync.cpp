#include <decentralized_path_auction/path_sync.hpp>

namespace decentralized_path_auction {

static void insertBids(const std::string& agent_id, const Path& path, float stop_duration) {
    Auction::Bid* prev_bid = nullptr;
    auto next_visit = path.begin();
    for (auto& visit : path) {
        // convert timestamp to duration
        float duration = ++next_visit == path.end() ? stop_duration : next_visit->time - visit.time;
        if (visit.node->auction.insertBid(agent_id, visit.price, duration, prev_bid)) {
            assert(!"insert bid failed");
        };
    }
}

static PathSync::Error removeBids(const std::string& agent_id, Path::const_iterator it, Path::const_iterator end) {
    bool error = false;
    for (; it != end; ++it) {
        error |= it->node->auction.removeBid(agent_id, it->price);
    }
    return error ? PathSync::VISIT_BID_ALREADY_REMOVED : PathSync::SUCCESS;
}

PathSync::Error PathSync::updatePath(
        const std::string& agent_id, const Path& path, size_t path_id, float stop_duration) {
    // input checks
    if (agent_id.empty()) {
        return AGENT_ID_EMPTY;
    }
    if (stop_duration < 0) {
        return STOP_DURATION_NEGATIVE;
    }
    if (auto path_error = validate(path)) {
        return path_error;
    }
    if (path.front().node->auction.getHighestBid(agent_id)->first > path.front().price) {
        return SOURCE_NODE_OUTBID;
    }
    auto& info = _paths[agent_id];
    // check if bid price is already taken by some other agent
    for (auto& visit : path) {
        auto& bids = visit.node->auction.getBids();
        auto found = bids.find(visit.price);
        if (found != bids.end() && found->second.bidder != agent_id) {
            return VISIT_PRICE_ALREADY_EXIST;
        }
    }
    // update path id
    if (path_id > info.path_id) {
        info.path_id = path_id;
    } else if (!info.path.empty()) {
        return PATH_ID_STALE;
    }
    // remove old bids
    auto error = removeBids(agent_id, info.path.begin() + info.progress, info.path.end());
    // update path
    info.path = path;
    info.progress = 0;
    insertBids(agent_id, path, stop_duration);
    return error;
}

PathSync::Error PathSync::updateProgress(const std::string& agent_id, size_t progress, size_t path_id) {
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return AGENT_ID_NOT_FOUND;
    }
    auto& info = found->second;
    if (path_id != info.path_id) {
        return PATH_ID_MISMATCH;
    }
    if (progress >= info.path.size()) {
        return PROGRESS_EXCEED_PATH_SIZE;
    }
    if (progress < info.progress) {
        return PROGRESS_DECREASE_DENIED;
    }
    // remove bids upto the new progress
    auto error = removeBids(agent_id, info.path.begin() + info.progress, info.path.begin() + progress);
    info.progress = progress;
    return error;
}

PathSync::Error PathSync::removePath(const std::string& agent_id) {
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return AGENT_ID_NOT_FOUND;
    }
    auto error = removeBids(agent_id, found->second.path.begin() + found->second.progress, found->second.path.end());
    _paths.erase(found);
    return error;
}

PathSync::Error PathSync::clearPaths() {
    int error = SUCCESS;
    for (auto& [agent_id, info] : _paths) {
        error |= removeBids(agent_id, info.path.begin() + info.progress, info.path.end());
    }
    _paths.clear();
    return static_cast<Error>(error);
}

PathSync::Error PathSync::getEntitledSegment(const std::string& agent_id, Path& segment) const {
    segment.clear();
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return AGENT_ID_NOT_FOUND;
    }
    auto& info = found->second;
    assert(info.progress < info.path.size());
    for (auto visit = info.path.begin() + info.progress;
            visit != info.path.end() && visit->node->auction.getHighestBid()->second.bidder == agent_id; ++visit) {
        if (auto visit_error = validate(*visit)) {
            return visit_error;
        }
        segment.push_back(*visit);
    }
    return segment.empty() ? SOURCE_NODE_OUTBID : SUCCESS;
}

bool PathSync::detectCycle() const {
    _cycle_visits.clear();
    for (const auto& info : _paths) {
        assert(info.second.path.size());
        auto& visit = info.second.path[info.second.progress];
        assert(visit.node);
        auto& bids = visit.node->auction.getBids();
        auto bid = bids.find(visit.price);
        if (bid != bids.end() && bid->second.detectCycle(_cycle_visits)) {
            return true;
        }
    }
    return false;
}

PathSync::Error PathSync::validate(const Visit& visit) const {
    if (!Node::validate(visit.node)) {
        return VISIT_NODE_INVALID;
    }
    if (visit.node->state == Node::DISABLED) {
        return VISIT_NODE_DISABLED;
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
    _unique_visits.clear();
    auto prev_visit_time = path.front().time;
    for (auto& visit : path) {
        if (auto error = validate(visit)) {
            return error;
        }
        if (visit.time < prev_visit_time) {
            return PATH_TIME_DECREASED;
        }
        if (!_unique_visits.emplace(visit.node.get(), visit.price).second) {
            return PATH_VISIT_DUPLICATED;
        }
        prev_visit_time = visit.time;
    }
    return SUCCESS;
}

}  // namespace decentralized_path_auction
