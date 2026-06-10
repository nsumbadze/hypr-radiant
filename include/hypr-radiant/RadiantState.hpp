#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hypr_radiant {

struct RadiantPoint {
    double x = 0.0;
    double y = 0.0;
};

struct RadiantSize {
    double width  = 0.0;
    double height = 0.0;
};

struct RadiantGeometry {
    RadiantPoint position;
    RadiantSize  size;
};

struct MonitorSnapshot {
    std::int64_t  id                = -1;
    std::string   name;
    RadiantGeometry geometry;
    float         scale             = 1.0F;
    int           transform         = 0;
    std::int64_t  activeWorkspaceId = -1;
    std::string   activeWorkspaceName;
};

struct WorkspaceSnapshot {
    std::int64_t id             = -1;
    std::string  name;
    std::int64_t monitorId      = -1;
    std::string  monitorName;
    bool         visible        = false;
    bool         special        = false;
    int          fullscreenMode = 0;
};

struct WindowSnapshot {
    std::uint64_t stableId    = 0;
    std::string   title;
    std::string   className;
    RadiantGeometry geometry;
    std::int64_t  workspaceId = -1;
    std::string   workspaceName;
    std::int64_t  monitorId   = -1;
    std::string   monitorName;
    bool          mapped      = false;
    bool          floating    = false;
    bool          fullscreen  = false;
    bool          pinned      = false;
};

struct RadiantState {
    std::vector<MonitorSnapshot>   monitors;
    std::vector<WorkspaceSnapshot> workspaces;
    std::vector<WindowSnapshot>    windows;

    [[nodiscard]] std::size_t mappedWindowCount() const noexcept {
        std::size_t count = 0;

        for (const auto& window : windows) {
            if (window.mapped)
                ++count;
        }

        return count;
    }
};

} // namespace hypr_radiant
