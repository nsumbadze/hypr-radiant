#include <hypr-radiant/overview/SearchMatcher.hpp>
#include <hypr-radiant/overview/SearchSuggestions.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

WorkspaceWallFrame frame() {
    WorkspaceWallFrame frame{.monitorId = 1, .bounds = {.width = 500, .height = 300}};
    frame.workspaces.push_back({.workspaceId = 1, .name = "1", .rect = {.x = 10, .y = 10, .width = 220, .height = 120}, .empty = false});
    frame.workspaces.back().windows.push_back({.stableId = 11, .workspaceId = 1, .rect = {.x = 30, .y = 30, .width = 90, .height = 50}, .label = "OpenCode"});
    frame.workspaces.back().windows.push_back({.stableId = 12, .workspaceId = 1, .rect = {.x = 130, .y = 30, .width = 90, .height = 50}, .label = "Chromium"});
    frame.workspaces.push_back({.workspaceId = 2, .name = "2", .rect = {.x = 250, .y = 10, .width = 220, .height = 120}, .empty = false});
    frame.workspaces.back().windows.push_back({.stableId = 21, .workspaceId = 2, .rect = {.x = 270, .y = 30, .width = 90, .height = 50}, .label = "Discord"});
    return frame;
}

RadiantState state() {
    RadiantState state;
    state.workspaces = {
        {.id = 1, .name = "dev", .monitorId = 4},
        {.id = 2, .name = "web", .monitorId = 4},
        {.id = -99, .name = "special:scratch", .monitorId = 4, .special = true},
    };
    state.windows = {
        {.stableId = 11, .title = "Issue tracker", .className = "org.mozilla.Firefox", .workspaceId = 2, .monitorId = 4, .mapped = true},
        {.stableId = 12, .title = "Documentation", .className = "ORG.MOZILLA.FIREFOX", .workspaceId = 1, .monitorId = 4, .mapped = true},
        {.stableId = 21, .title = "Editor", .className = "code", .workspaceId = 1, .monitorId = 4, .mapped = true},
        {.stableId = 99, .title = "Hidden", .className = "hidden", .workspaceId = 1, .monitorId = 4, .mapped = false},
    };
    return state;
}

void matchesCaseInsensitiveSubstring() {
    assert(SearchMatcher{}.matches("OpenCode", "code"));
    assert(SearchMatcher{}.matches("OpenCode", "OPEN"));
    assert(!SearchMatcher{}.matches("OpenCode", "discord"));
}

void emptyQueryHasNoMatches() {
    assert(SearchMatcher{}.matchingWindowIds(frame(), "").empty());
}

void returnsAllMatchingWindowIdsInLayoutOrder() {
    const auto matches = SearchMatcher{}.matchingWindowIds(frame(), "o");
    assert(matches.size() == 3);
    assert(matches[0] == 11);
    assert(matches[1] == 12);
    assert(matches[2] == 21);
}

void stageSearchMatchesApplicationClass() {
    auto testFrame = frame();
    testFrame.stage.windows.push_back({.stableId = 88, .workspaceId = 2, .label = "Private tab", .appClass = "Firefox"});

    const auto matches = SearchMatcher{}.matchingStageWindowIds(testFrame, "fire");

    assert(matches.size() == 1);
    assert(matches.front() == 88);
}

void emptySearchOffersEveryRunningAppWindowAndWorkspace() {
    const auto suggestions = buildSearchSuggestions(state(), "");

    assert(suggestions.size() == 7);
    assert(suggestions[0].kind == SearchSuggestionKind::Application);
    assert(suggestions[0].label == "Code");
    assert(suggestions[0].windowCount == 1);
    assert(suggestions[1].kind == SearchSuggestionKind::Application);
    assert(suggestions[1].label == "Firefox");
    assert(suggestions[1].windowCount == 2);
    assert(suggestions[1].target.type == OverviewTargetType::Application);
    assert(suggestions[2].kind == SearchSuggestionKind::Window);
    assert(suggestions[5].kind == SearchSuggestionKind::Workspace);
    assert(suggestions[5].target.workspaceId == 1);
    assert(suggestions[6].target.workspaceId == 2);
}

void applicationClassFindsItsAppAndEveryWindow() {
    const auto suggestions = buildSearchSuggestions(state(), "fire");

    assert(suggestions.size() == 3);
    assert(suggestions.front().kind == SearchSuggestionKind::Application);
    assert(suggestions.front().windowCount == 2);
    assert(suggestions[1].kind == SearchSuggestionKind::Window);
    assert(suggestions[2].kind == SearchSuggestionKind::Window);
}

void titleSearchCanReturnOneSpecificWindow() {
    const auto suggestions = buildSearchSuggestions(state(), "editor");

    assert(suggestions.size() == 1);
    assert(suggestions.front().kind == SearchSuggestionKind::Window);
    assert(suggestions.front().target.windowId == 21);
}

void workspaceNameIsSearchableAndSpecialWorkspaceIsHidden() {
    const auto suggestions = buildSearchSuggestions(state(), "web");

    assert(suggestions.size() == 1);
    assert(suggestions.front().kind == SearchSuggestionKind::Workspace);
    assert(suggestions.front().target.workspaceId == 2);
}

} // namespace

int main() {
    matchesCaseInsensitiveSubstring();
    emptyQueryHasNoMatches();
    returnsAllMatchingWindowIdsInLayoutOrder();
    stageSearchMatchesApplicationClass();
    emptySearchOffersEveryRunningAppWindowAndWorkspace();
    applicationClassFindsItsAppAndEveryWindow();
    titleSearchCanReturnOneSpecificWindow();
    workspaceNameIsSearchableAndSpecialWorkspaceIsHidden();
    std::cout << "SearchMatcherTest passed\n";
    return 0;
}
