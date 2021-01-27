#pragma once

#include <decentralized_path_auction/path_search.hpp>

#include <unordered_map>

namespace decentralized_path_auction {

class PathSync {
public:
    enum Error {
        SUCCESS,
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
        size_t path_id;
        size_t progress;
    };

    using Paths = std::unordered_map<std::string, PathInfo>;

    Error updatePath(const std::string& agent_id, Path path, float stop_duration = std::numeric_limits<float>::max(),
            size_t path_id = 0);

    Error updateProgress(const std::string& agent_id, size_t progress, size_t path_id = 0);

    size_t furthestReachable(const std::string& agent_id) const;

    const Paths& getPaths() const { return _paths; }

private:
    Paths _paths;
};

}  // namespace decentralized_path_auction
