#include <hypr-radiant/SearchMatcher.hpp>

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

void matchesCaseInsensitiveSubstring() {
    assert(SearchMatcher{}.matches("OpenCode", "code"));
    assert(SearchMatcher{}.matches("OpenCode", "OPEN"));
    assert(!SearchMatcher{}.matches("OpenCode", "discord"));
}

void emptyQueryHasNoMatches() {
    assert(SearchMatcher{}.matchingWindowIds(frame(), "").empty());
    assert(!SearchMatcher{}.firstMatch(frame(), "").has_value());
}

void returnsAllMatchingWindowIdsInLayoutOrder() {
    const auto matches = SearchMatcher{}.matchingWindowIds(frame(), "o");
    assert(matches.size() == 3);
    assert(matches[0] == 11);
    assert(matches[1] == 12);
    assert(matches[2] == 21);
}

void firstMatchReturnsWindowTarget() {
    const auto target = SearchMatcher{}.firstMatch(frame(), "disc");
    assert(target.has_value());
    assert(target->type == OverviewTargetType::Window);
    assert(target->workspaceId == 2);
    assert(target->windowId == 21);
}

} // namespace

int main() {
    matchesCaseInsensitiveSubstring();
    emptyQueryHasNoMatches();
    returnsAllMatchingWindowIdsInLayoutOrder();
    firstMatchReturnsWindowTarget();
    std::cout << "SearchMatcherTest passed\n";
    return 0;
}
