#include <hypr-radiant/overview/PreferencesPanelGeometry.hpp>

#include <algorithm>

namespace hypr_radiant {
namespace {

bool contains(const LayoutRect& rect, double x, double y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

} // namespace

PreferencesPanelFrame computePreferencesPanel(const LayoutRect& monitorBounds) {
    constexpr auto preferredWidth  = 720.0;
    constexpr auto preferredHeight = 390.0;
    constexpr auto outerMargin     = 28.0;
    constexpr auto rowHeight       = 46.0;

    const auto width  = std::max(1.0, std::min(preferredWidth, monitorBounds.width - outerMargin * 2.0));
    const auto height = std::max(1.0, std::min(preferredHeight, monitorBounds.height - outerMargin * 2.0));
    const LayoutRect panel{
        .x      = monitorBounds.x + (monitorBounds.width - width) / 2.0,
        .y      = monitorBounds.y + (monitorBounds.height - height) / 2.0,
        .width  = width,
        .height = height,
    };

    const auto rowX     = panel.x + 28.0;
    const auto rowWidth = std::max(1.0, panel.width - 56.0);
    const std::array rowOffsets{102.0, 157.0, 242.0};
    const std::array controls{
        PreferenceControl::WorkspaceView,
        PreferenceControl::WindowView,
        PreferenceControl::Accent,
    };

    PreferencesPanelFrame frame{
        .panel = panel,
        .closeButton = {
            .x      = panel.x + panel.width - 48.0,
            .y      = panel.y + 20.0,
            .width  = 26.0,
            .height = 26.0,
        },
        .rows            = {},
        .options         = {},
        .appExposeButton = {},
    };
    for (std::size_t i = 0; i < frame.rows.size(); ++i) {
        frame.rows[i] = {
            .control = controls[i],
            .rect = {
                .x      = rowX,
                .y      = panel.y + rowOffsets[i],
                .width  = rowWidth,
                .height = rowHeight,
            },
        };
    }

    constexpr std::array optionCounts{2, 2, 4};
    std::size_t optionIndex = 0;
    for (std::size_t rowIndex = 0; rowIndex < frame.rows.size(); ++rowIndex) {
        const auto& row = frame.rows[rowIndex];
        const auto optionCount = optionCounts[rowIndex];
        constexpr auto optionGap = 5.0;
        const auto optionsX = row.rect.x + std::min(238.0, row.rect.width * 0.42);
        const auto optionsWidth = std::max(1.0, row.rect.x + row.rect.width - optionsX);
        const auto optionWidth = std::max(1.0, (optionsWidth - optionGap * static_cast<double>(optionCount - 1)) / static_cast<double>(optionCount));
        for (int value = 0; value < optionCount; ++value) {
            frame.options[optionIndex++] = {
                .control = row.control,
                .value   = value,
                .rect = {
                    .x = optionsX + static_cast<double>(value) * (optionWidth + optionGap),
                    .y = row.rect.y + 7.0,
                    .width = optionWidth,
                    .height = row.rect.height - 14.0,
                },
            };
        }
    }

    frame.appExposeButton = {
        .x      = rowX,
        .y      = panel.y + panel.height - 82.0,
        .width  = rowWidth,
        .height = 34.0,
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

} // namespace hypr_radiant
