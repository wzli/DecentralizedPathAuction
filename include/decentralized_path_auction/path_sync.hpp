#pragma once

#include <unordered_map>
#include <decentralized_path_auction/graph.hpp>

namespace decentralized_path_auction {

class PathSync {
public:
    enum Error {
        SUCCESS,
        REMAINING_DURATION_INFINITE,
        SOURCE_NODE_OUTBID,
        DESTINATION_NODE_NO_PARKING,
        VISIT_NODE_INVALID,
        VISIT_NODE_DISABLED,
        VISIT_DURATION_NEGATIVE,
        VISIT_PRICE_ALREADY_EXIST,
        VISIT_PRICE_LESS_THAN_START_PRICE,
        VISIT_BID_ALREADY_REMOVED,
        PATH_EMPTY,
        PATH_VISIT_DUPLICATED,
        PATH_CAUSES_CYCLE,
        PATH_ID_STALE,
        PATH_ID_MISMATCH,
        AGENT_ID_EMPTY,
        AGENT_ID_NOT_FOUND,
        PROGRESS_DECREASE_DENIED,
        PROGRESS_EXCEED_PATH_SIZE,
    };

    struct PathInfo {
        Path path;
        size_t path_id = 0;
        size_t progress = 0;
    };

    struct WaitStatus {
        Error error;
        size_t blocked_progress;
        float remaining_duration;
    };

    using Paths = std::unordered_map<std::string, PathInfo>;

    // non-copyable but movable
    ~PathSync() { clearPaths(); }
    PathSync& operator=(PathSync&& rhs) { return clearPaths(), _paths.swap(rhs._paths), *this; }

    Error updatePath(const std::string& agent_id, const Path& path, size_t path_id);
    Error updateProgress(const std::string& agent_id, size_t progress, size_t path_id);

    // remove all bids from auction when path is removed
    Error removePath(const std::string& agent_id);
    Error clearPaths();

    WaitStatus checkWaitStatus(const std::string& agent_id) const;

    const Paths& getPaths() const { return _paths; }

    Error validate(const Visit& visit) const;
    Error validate(const Path& path) const;

private:
    Paths _paths;
};

}  // namespace decentralized_path_auction
