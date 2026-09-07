#include <hypr-radiant/overview/PreferencesPanelGeometry.hpp>

#include <algorithm>

namespace hypr_radiant {
namespace {

bool contains(const LayoutRect& rect, double x, double y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

} // namespace

PreferencesPanelFrame computePreferencesPanel(
    const LayoutRect& monitorBounds, bool includeWindowArrangement, int nativeThemeOptionCount) {
    const auto showNativeThemes = nativeThemeOptionCount > 0;
    std::vector<PreferenceControl> controls{PreferenceControl::WorkspaceView};
    if (includeWindowArrangement) {
        controls.push_back(PreferenceControl::WindowView);
        controls.push_back(PreferenceControl::Shelf);
    }
    controls.push_back(PreferenceControl::WindowNavigation);
    controls.push_back(PreferenceControl::Motion);
    controls.push_back(PreferenceControl::Chrome);
    if (showNativeThemes)
        controls.push_back(PreferenceControl::NativeTheme);
    const auto preferredWidth = showNativeThemes ? 820.0 : 720.0;
    const auto preferredHeight = 122.0 + static_cast<double>(controls.size()) * 60.0;
    constexpr auto outerMargin     = 28.0;

    const auto width  = std::max(1.0, std::min(preferredWidth, monitorBounds.width - outerMargin * 2.0));
    const auto height = std::max(1.0, std::min(preferredHeight, monitorBounds.height - outerMargin * 2.0));
    const LayoutRect panel{
        .x      = monitorBounds.x + (monitorBounds.width - width) / 2.0,
        .y      = monitorBounds.y + (monitorBounds.height - height) / 2.0,
        .width  = width,
        .height = height,
    };

    // Keep the header clear of the fixed-size close button, even when rows shrink.
    constexpr auto headerHeight = 46.0;
    const auto verticalScale = std::clamp((height - headerHeight) / (preferredHeight - headerHeight), 0.0, 1.0);
    const auto rowHeight     = 52.0 * verticalScale;
    const auto rowGap        = 8.0 * verticalScale;
    const auto rowX          = panel.x + 22.0;
    const auto innerWidth    = std::max(1.0, panel.width - 44.0);
    const auto rowWidth = innerWidth;
    const auto paneY = panel.y + std::min(headerHeight, height);
    const auto paneHeight = std::max(1.0, panel.height - 68.0 * verticalScale);
    PreferencesPanelFrame frame{
        .panel = panel,
        .closeButton = {
            .x      = panel.x + panel.width - 38.0,
            .y      = panel.y + 12.0,
            .width  = 24.0,
            .height = 24.0,
        },
        .rows         = {},
        .options      = {},
        .settingsPane = {
            .x      = rowX,
            .y      = paneY,
            .width  = rowWidth,
            .height = paneHeight,
        },
        .nativeThemesPane = {},
        .appExposeButton  = {},
    };
    frame.rows.reserve(controls.size());
    for (std::size_t index = 0; index < controls.size(); ++index) {
        frame.rows.push_back({
            .control = controls[index],
            .rect = {
                .x      = rowX,
                .y      = paneY + static_cast<double>(index) * (rowHeight + rowGap),
                .width  = rowWidth,
                .height = rowHeight,
            },
        });
    }

    const auto optionCountFor = [](PreferenceControl control) {
        switch (control) {
        case PreferenceControl::WorkspaceView:
            return 4;
        case PreferenceControl::WindowView:
        case PreferenceControl::WindowNavigation:
            return 3;
        case PreferenceControl::Shelf:
        case PreferenceControl::Chrome:
            return 4;
        case PreferenceControl::Motion:
            return 7;
        case PreferenceControl::NativeTheme:
            return 3;
        case PreferenceControl::None:
        case PreferenceControl::AppExpose:
        case PreferenceControl::Close:
            return 0;
        }
        return 0;
    };
    frame.options.reserve(controls.size() * 4);
    for (const auto& row : frame.rows) {
        const auto optionCount = optionCountFor(row.control);
        constexpr auto optionGap = 6.0;
        const auto optionsStart = row.rect.x + std::min(154.0, row.rect.width * 0.24);
        const auto availableWidth = std::max(1.0, row.rect.x + row.rect.width - optionsStart);
        const auto preferredOptionsWidth = row.control == PreferenceControl::NativeTheme ? 480.0 : availableWidth;
        const auto optionsWidth = std::min(availableWidth, preferredOptionsWidth);
        const auto optionsX = row.rect.x + row.rect.width - optionsWidth;
        if (row.control == PreferenceControl::NativeTheme) {
            constexpr auto arrowWidth = 42.0;
            const auto centerWidth = std::max(1.0, optionsWidth - arrowWidth * 2.0 - optionGap * 2.0);
            frame.options.push_back({
                .control = row.control,
                .value   = 0,
                .rect = {
                    .x      = optionsX,
                    .y      = row.rect.y + 8.0 * verticalScale,
                    .width  = arrowWidth,
                    .height = row.rect.height - 16.0 * verticalScale,
                },
            });
            frame.options.push_back({
                .control = row.control,
                .value   = 1,
                .rect = {
                    .x      = optionsX + arrowWidth + optionGap,
                    .y      = row.rect.y + 8.0 * verticalScale,
                    .width  = centerWidth,
                    .height = row.rect.height - 16.0 * verticalScale,
                },
            });
            frame.options.push_back({
                .control = row.control,
                .value   = 2,
                .rect = {
                    .x      = optionsX + arrowWidth + optionGap + centerWidth + optionGap,
                    .y      = row.rect.y + 8.0 * verticalScale,
                    .width  = arrowWidth,
                    .height = row.rect.height - 16.0 * verticalScale,
                },
            });
            continue;
        }
        const auto optionWidth = std::max(1.0, (optionsWidth - optionGap * static_cast<double>(optionCount - 1)) / static_cast<double>(optionCount));
        for (int value = 0; value < optionCount; ++value) {
            frame.options.push_back({
                .control = row.control,
                .value   = value,
                .rect = {
                    .x = optionsX + static_cast<double>(value) * (optionWidth + optionGap),
                    .y = row.rect.y + 8.0 * verticalScale,
                    .width = optionWidth,
                    .height = row.rect.height - 16.0 * verticalScale,
                },
            });
        }
    }

    frame.appExposeButton = {
        .x      = rowX,
        .y      = panel.y + panel.height - 56.0 * verticalScale,
        .width  = rowWidth,
        .height = 34.0 * verticalScale,
    };
    return frame;
}

PreferenceHit hitTestPreferencesPanel(const PreferencesPanelFrame& frame, double x, double y) {
    if (contains(frame.closeButton, x, y))
        return {.control = PreferenceControl::Close};
    for (const auto& option : frame.options) {
        if (contains(option.rect, x, y))
            return {.control = option.control, .value = option.value};
    }
    for (const auto& row : frame.rows) {
        if (contains(row.rect, x, y))
            return {.control = row.control};
    }
    if (contains(frame.appExposeButton, x, y))
        return {.control = PreferenceControl::AppExpose};
    return {};
}

bool containsPreferencesPanel(const PreferencesPanelFrame& frame, double x, double y) {
    return contains(frame.panel, x, y);
}

} // namespace hypr_radiant
