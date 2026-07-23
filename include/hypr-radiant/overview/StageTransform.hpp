#pragma once

#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

#include <optional>

namespace hypr_radiant {

[[nodiscard]] LayoutRect remapStageRect(const LayoutRect& child, const LayoutRect& source, const LayoutRect& target);
[[nodiscard]] std::optional<RadiantPoint> mapStagePointToSource(const LayoutRect& source, const LayoutRect& target, RadiantPoint point);

} // namespace hypr_radiant
