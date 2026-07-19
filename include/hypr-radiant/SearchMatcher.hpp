#pragma once

#include <hypr-radiant/HitTester.hpp>
#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hypr_radiant {

class SearchMatcher {
  public:
    [[nodiscard]] bool matches(std::string_view label, std::string_view query) const;
    [[nodiscard]] std::vector<std::uint64_t> matchingWindowIds(const WorkspaceWallFrame& frame, std::string_view query) const;
    [[nodiscard]] std::vector<std::uint64_t> matchingStageWindowIds(const WorkspaceWallFrame& frame, std::string_view query) const;
    [[nodiscard]] std::optional<OverviewTarget> firstMatch(const WorkspaceWallFrame& frame, std::string_view query) const;
};

} // namespace hypr_radiant
