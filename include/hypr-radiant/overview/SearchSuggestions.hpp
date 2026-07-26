#pragma once

#include <hypr-radiant/OverviewTarget.hpp>
#include <hypr-radiant/RadiantState.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hypr_radiant {

enum class SearchSuggestionKind {
    Application,
    Window,
    Workspace,
};

struct SearchSuggestion {
    SearchSuggestionKind kind = SearchSuggestionKind::Window;
    OverviewTarget       target;
    std::string          label;
    std::string          context;
    std::string          appClass;
    std::size_t          windowCount = 0;
};

[[nodiscard]] std::vector<SearchSuggestion> buildSearchSuggestions(const RadiantState& state, std::string_view query);

} // namespace hypr_radiant
