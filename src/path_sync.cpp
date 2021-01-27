#include <decentralized_path_auction/path_sync.hpp>

namespace decentralized_path_auction {

static void insertPathBids(const std::string& agent_id, const Path& path, float stop_duration) {
    Auction::Bid* prev_bid = nullptr;
    for (auto& visit : path) {
        // convert timestamp to duration
        float duration = &visit == &path.back() ? stop_duration : (&visit + 1)->time - visit.time;
        assert(duration >= 0);
        if (!visit.node->auction.insertBid(agent_id, visit.price, duration, prev_bid)) {
            assert(0 && "insert bid failed");
        };
    }
}

static void removePathBids(const std::string& agent_id, Path::iterator it, Path::iterator end) {
    for (; it != end; ++it) {
        if (!it->node->auction.removeBid(agent_id, it->price)) {
            assert(0 && "remove bid failed");
        }
    }
}

PathSync::Error PathSync::updatePath(const std::string& agent_id, Path path, float stop_duration, size_t path_id) {
    if (agent_id.empty()) {
        return AGENT_ID_EMPTY;
    }
    if (stop_duration < 0) {
        return STOP_DURATION_NEGATIVE;
    }
    auto& info = _paths[agent_id];
    // update path id
    if (path_id == 0) {
        ++info.path_id;
    } else if (path_id > info.path_id) {
        info.path_id = path_id;
    } else {
        return PATH_ID_STALE;
    }
    // remove old bids
    removePathBids(agent_id, info.path.begin(), info.path.end());
    insertPathBids(agent_id, path, stop_duration);
    // save path (delete if path is empty)
    if (path.empty()) {
        _paths.erase(agent_id);
    } else {
        info.path = std::move(path);
        info.progress = 0;
    }
    return SUCCESS;
}

PathSync::Error PathSync::updateProgress(const std::string& agent_id, size_t progress, size_t path_id) {
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return AGENT_ID_NOT_FOUND;
    }
    auto& info = found->second;
    if (path_id != 0 && path_id != info.path_id) {
        return PATH_ID_MISMATCH;
    }
    if (progress >= info.path.size()) {
        return PROGRESS_EXCEED_PATH_SIZE;
    }
    if (progress < info.progress) {
        return PROGRESS_DECREASE_DENIED;
    }
    removePathBids(agent_id, info.path.begin() + info.progress, info.path.begin() + progress);
    info.progress = progress;
    return SUCCESS;
}

size_t PathSync::furthestReachable(const std::string& agent_id) const {
    auto found = _paths.find(agent_id);
    if (found == _paths.end()) {
        return 0;
    }
    auto& info = found->second;
    size_t i = info.progress;
    while (i + 1 < info.path.size() && agent_id == info.path[i + 1].node->auction.getHighestBid()->second.bidder) {
        ++i;
    }
    return i;
}

}  // namespace decentralized_path_auction
