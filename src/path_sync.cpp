#include <decentralized_path_auction/path_sync.hpp>

namespace decentralized_path_auction {

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
    Visit::removeBids(info.path.begin(), info.path.end(), agent_id);
    Visit::insertBids(path, agent_id, stop_duration);
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
    Visit::removeBids(info.path.begin() + info.progress, info.path.begin() + progress, agent_id);
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
