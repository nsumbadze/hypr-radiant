#include <hypr-radiant/overview/SearchSuggestions.hpp>

#include <hypr-radiant/overview/SearchMatcher.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <unordered_map>

namespace hypr_radiant {
namespace {

std::string lowercase(std::string_view value) {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::string appLabel(std::string_view appClass) {
    const auto separator = appClass.find_last_of('.');
    std::string label{separator == std::string_view::npos ? appClass : appClass.substr(separator + 1)};
    std::ranges::replace(label, '-', ' ');
    std::ranges::replace(label, '_', ' ');
    if (!label.empty())
        label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
    return label;
}

bool searchable(const SearchMatcher& matcher, std::string_view query, std::string_view primary, std::string_view secondary = {}) {
    return query.empty() || matcher.matches(primary, query) || (!secondary.empty() && matcher.matches(secondary, query));
}

} // namespace

std::vector<SearchSuggestion> buildSearchSuggestions(const RadiantState& state, std::string_view query) {
    const SearchMatcher matcher;
    std::vector<SearchSuggestion> applications;
    std::vector<SearchSuggestion> windows;
    std::vector<SearchSuggestion> workspaces;
    std::unordered_map<std::string, std::size_t> applicationByClass;

    for (const auto& window : state.windows) {
        if (!window.mapped)
            continue;

        if (!window.className.empty()) {
            const auto key = lowercase(window.className);
            const auto [entry, inserted] = applicationByClass.try_emplace(key, applications.size());
            if (inserted) {
                applications.push_back({
                    .kind   = SearchSuggestionKind::Application,
                    .target = {
                        .type = OverviewTargetType::Application,
                        .workspaceId = window.workspaceId,
                        .windowId = window.stableId,
                        .monitorId = window.monitorId,
                               },
                    .label = appLabel(window.className),
                    .context = {},
                    .appClass = window.className,
                    .windowCount = 0,
                });
            }
            ++applications[entry->second].windowCount;
        }

        const auto title = window.title.empty() ? appLabel(window.className) : window.title;
        if (searchable(matcher, query, title, window.className)) {
            windows.push_back({
                .kind   = SearchSuggestionKind::Window,
                .target = {
                    .type = OverviewTargetType::Window,
                    .workspaceId = window.workspaceId,
                    .windowId = window.stableId,
                    .monitorId = window.monitorId,
                },
                .label = title,
                .context = std::format("{}  ·  workspace {}", appLabel(window.className), window.workspaceId),
                .appClass = window.className,
            });
        }
    }

    std::erase_if(applications, [&](const SearchSuggestion& suggestion) {
        return !searchable(matcher, query, suggestion.label, suggestion.appClass);
    });
    for (auto& application : applications) {
        application.context = std::format("{} window{}  ·  open workspace",
            application.windowCount, application.windowCount == 1 ? "" : "s");
    }

    for (const auto& workspace : state.workspaces) {
        if (workspace.special)
            continue;
        const auto number = std::to_string(workspace.id);
        if (!searchable(matcher, query, workspace.name, number))
            continue;
        workspaces.push_back({
            .kind   = SearchSuggestionKind::Workspace,
            .target = {
                .type = OverviewTargetType::Workspace,
                .workspaceId = workspace.id,
                .monitorId = workspace.monitorId,
            },
            .label = workspace.name.empty() ? std::format("Workspace {}", workspace.id) : std::format("Workspace {}", workspace.name),
            .context  = "switch workspace",
            .appClass = {},
            .windowCount = 0,
        });
    }

    const auto byLabel = [](const SearchSuggestion& lhs, const SearchSuggestion& rhs) {
        return lowercase(lhs.label) < lowercase(rhs.label);
    };
    std::ranges::sort(applications, byLabel);
    std::ranges::sort(windows, byLabel);
    std::ranges::sort(workspaces, [](const SearchSuggestion& lhs, const SearchSuggestion& rhs) {
        return lhs.target.workspaceId < rhs.target.workspaceId;
    });

    std::vector<SearchSuggestion> suggestions;
    suggestions.reserve(applications.size() + windows.size() + workspaces.size());
    suggestions.insert(suggestions.end(), applications.begin(), applications.end());
    suggestions.insert(suggestions.end(), windows.begin(), windows.end());
    suggestions.insert(suggestions.end(), workspaces.begin(), workspaces.end());
    return suggestions;
}

} // namespace hypr_radiant
