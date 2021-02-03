#pragma once

#include <decentralized_path_auction/graph.hpp>
#include <unordered_map>

namespace decentralized_path_auction {

class PathSync {
public:
    enum Error {
        SUCCESS,
        VISIT_NODE_INVALID,
        VISIT_PRICE_NOT_ABOVE_MIN_PRICE,
        VISIT_MIN_PRICE_NOT_ABOVE_START_PRICE,
        PATH_TIME_DECREASED,
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
    };

    using Paths = std::unordered_map<std::string, PathInfo>;

    Error updatePath(const std::string& agent_id, const Path& path, size_t path_id,
            float stop_duration = std::numeric_limits<float>::max());
    Error removePath(const std::string& agent_id);
    Error updateProgress(const std::string& agent_id, size_t progress, size_t path_id);

    Error getEntitledSegment(const std::string& agent_id, Path& segment) const;
    const Paths& getPaths() const { return _paths; }

    Error validate(const Visit& visit) const;
    Error validate(const Path& path) const;

private:
    Paths _paths;
};

}  // namespace decentralized_path_auction
