#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <unordered_map>

namespace decentralized_path_auction {

class PathSync {
public:
    enum Error {
        SUCCESS,
        SOURCE_NODE_OUTBID,
        VISIT_NODE_INVALID,
        VISIT_NODE_DISABLED,
        VISIT_PRICE_ALREADY_EXIST,
        VISIT_PRICE_LESS_THAN_START_PRICE,
        VISIT_BID_ALREADY_REMOVED,
        PATH_CAUSES_CYCLE,
        PATH_TIME_DECREASED,
        PATH_VISIT_DUPLICATED,
        PATH_EMPTY,
        PATH_ID_STALE,
        PATH_ID_MISMATCH,
        AGENT_ID_EMPTY,
        AGENT_ID_NOT_FOUND,
        PROGRESS_DECREASE_DENIED,
        PROGRESS_EXCEED_PATH_SIZE,
        STOP_DURATION_NEGATIVE,
    };

    struct PathInfo {
        Path path;
        size_t path_id = 0;
        size_t progress = 0;
        float stop_duration = FLT_MAX;
    };

    using Paths = std::unordered_map<std::string, PathInfo>;

    // non-copyable but movable
    ~PathSync() { clearPaths(); }
    PathSync& operator=(PathSync&& rhs) { return clearPaths(), _paths.swap(rhs._paths), *this; }

    Error updatePath(const std::string& agent_id, const Path& path, size_t path_id, float stop_duration = FLT_MAX);
    Error updateProgress(const std::string& agent_id, size_t progress, size_t path_id);

    // remove all bids from auction when path is removed
    Error removePath(const std::string& agent_id);
    Error clearPaths();

    Error getEntitledSegment(const std::string& agent_id, Path& segment) const;
    const Paths& getPaths() const { return _paths; }

    Error validate(const Visit& visit) const;
    Error validate(const Path& path) const;

private:
    Paths _paths;
    mutable std::vector<bool> _cycle_visits;
    mutable std::set<std::pair<const Node*, float>> _unique_visits;
};

}  // namespace decentralized_path_auction
