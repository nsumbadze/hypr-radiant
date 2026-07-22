#include <hypr-radiant/SearchMatcher.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace hypr_radiant {
namespace {

std::string lowercase(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto ch : value)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return result;
}

} // namespace

bool SearchMatcher::matches(std::string_view label, std::string_view query) const {
    if (query.empty())
        return false;

    return lowercase(label).find(lowercase(query)) != std::string::npos;
}

std::vector<std::uint64_t> SearchMatcher::matchingWindowIds(const WorkspaceWallFrame& frame, std::string_view query) const {
    std::vector<std::uint64_t> matches;
    if (query.empty())
        return matches;

    for (const auto& workspace : frame.workspaces) {
        for (const auto& window : workspace.windows) {
            if (this->matches(window.label, query))
                matches.push_back(window.stableId);
        }
    }

    return matches;
}

std::vector<std::uint64_t> SearchMatcher::matchingStageWindowIds(const WorkspaceWallFrame& frame, std::string_view query) const {
    std::vector<std::uint64_t> matches;
    for (const auto& window : frame.stage.windows) {
        if (query.empty() || this->matches(window.label, query) || this->matches(window.appClass, query))
            matches.push_back(window.stableId);
    }
    return matches;
}

} // namespace hypr_radiant
