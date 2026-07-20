#pragma once

#include <hyprland/src/helpers/Color.hpp>

namespace hypr_radiant::Theme {

// ── Radii ─────────────────────────────────────────────────────

[[nodiscard]] constexpr int workspaceRadius(bool compact) noexcept {
    return compact ? 14 : 22;
}

[[nodiscard]] constexpr int railRadius() noexcept {
    return 22;
}

[[nodiscard]] constexpr int windowRadius() noexcept {
    return 12;
}

[[nodiscard]] constexpr int searchRadius() noexcept {
    return 24;
}

[[nodiscard]] constexpr int inputRadius() noexcept {
    return 12;
}

// ── Shadow metrics ────────────────────────────────────────────

[[nodiscard]] constexpr double shadowOffsetX() noexcept {
    return 8.0;
}

[[nodiscard]] constexpr double shadowOffsetY() noexcept {
    return 12.0;
}

[[nodiscard]] constexpr double shadowBlur() noexcept {
    return 22.0;
}

// ── Typography sizes ──────────────────────────────────────────

[[nodiscard]] constexpr int titleSize() noexcept {
    return 18;
}

[[nodiscard]] constexpr int labelSize() noexcept {
    return 14;
}

[[nodiscard]] constexpr int footerSize() noexcept {
    return 12;
}

[[nodiscard]] constexpr int hintSize() noexcept {
    return 11;
}

[[nodiscard]] constexpr int badgeSize() noexcept {
    return 9;
}

// ── Colour functions ──────────────────────────────────────────
//
// Base palette from Omarchy snapshot:
//   background  #111c18  → (0.067, 0.110, 0.094)
//   foreground  #C1C497  → (0.757, 0.769, 0.592)
//   accent      #509475  → (0.314, 0.580, 0.459)
//   gradient_a  #33ccff  → (0.200, 0.800, 1.000)
//   gradient_b  #00ff99  → (0.000, 1.000, 0.600)
//
// Card/window fills retain the existing blue-gray values shifted
// toward the Omarchy green tint.  All colour functions are inline
// (not constexpr) because CHyprColor's constructors are not
// constexpr in the Hyprland ABI.

/// Semi-transparent backdrop that dims the desktop behind the overview.
[[nodiscard]] inline CHyprColor backdropColor() noexcept {
    return {0.067F, 0.110F, 0.094F, 0.78F};
}

/// Neutral backdrop used by the desktop/Omarchy rail-and-stage layout.
[[nodiscard]] inline CHyprColor stageBackdropColor() noexcept {
    return {0.018F, 0.020F, 0.024F, 0.76F};
}

[[nodiscard]] inline CHyprColor railColor() noexcept {
    return {0.038F, 0.043F, 0.047F, 0.78F};
}

[[nodiscard]] inline CHyprColor railCardFill(bool selected, bool empty, float alpha) noexcept {
    if (empty)
        return {0.030F, 0.035F, 0.038F, 0.62F * alpha};
    if (selected)
        return {0.090F, 0.102F, 0.103F, 0.91F * alpha};
    return {0.061F, 0.069F, 0.071F, 0.84F * alpha};
}

[[nodiscard]] inline CHyprColor stageWindowFill(float alpha) noexcept {
    return {0.055F, 0.058F, 0.064F, 0.94F * alpha};
}

/// Background panel that frames the workspace grid.
[[nodiscard]] inline CHyprColor panelColor() noexcept {
    return {0.018F, 0.021F, 0.030F, 0.90F};
}

/// Workspace card fill, selected by the (active, selected, empty) state
/// tuple.  `alpha` is the animation / visibility multiplier.
[[nodiscard]] inline CHyprColor cardFill(bool active, bool selected, bool empty, float alpha) noexcept {
    if (empty)
        return {0.039F, 0.043F, 0.058F, 0.72F * alpha};
    if (selected)
        return {0.110F, 0.118F, 0.142F, 0.98F * alpha};
    if (active)
        return {0.094F, 0.104F, 0.132F, 0.98F * alpha};
    return {0.078F, 0.084F, 0.106F, 0.96F * alpha};
}

/// Workspace card border, selected by state.  `alpha` is the animation
/// multiplier.
[[nodiscard]] inline CHyprColor cardBorder(bool active, bool selected, float alpha) noexcept {
    if (selected)
        return {0.314F, 0.580F, 0.459F, 0.96F * alpha};
    if (active)
        return {0.314F, 0.580F, 0.459F, 0.30F * alpha};
    return {0.000F, 0.000F, 0.000F, 0.0F};
}

/// Outer glow drawn around the selected card.
[[nodiscard]] inline CHyprColor activeGlowColor() noexcept {
    return {0.314F, 0.580F, 0.459F, 0.12F};
}

/// Primary accent colour (#509475).
[[nodiscard]] inline CHyprColor accentColor() noexcept {
    return {0.314F, 0.580F, 0.459F, 1.0F};
}

/// Cool signal used sparingly at the leading edge of animated rail accents.
[[nodiscard]] inline CHyprColor signalColor() noexcept {
    return {0.200F, 0.800F, 1.000F, 1.0F};
}

/// Text colour (#C1C497).
[[nodiscard]] inline CHyprColor textColor() noexcept {
    return {0.757F, 0.769F, 0.592F, 1.0F};
}

/// Drop-shadow colour.
[[nodiscard]] inline CHyprColor shadowColor() noexcept {
    return {0.000F, 0.000F, 0.000F, 0.45F};
}

/// Window card fill.  `alpha` is the animation multiplier.
[[nodiscard]] inline CHyprColor windowFill(bool selected, float alpha) noexcept {
    if (selected)
        return {0.122F, 0.130F, 0.160F, 0.96F * alpha};
    return {0.106F, 0.114F, 0.140F, 0.96F * alpha};
}

/// Window card border.  `alpha` is the animation multiplier.
[[nodiscard]] inline CHyprColor windowBorder(bool selected, float alpha) noexcept {
    if (selected)
        return {0.314F, 0.580F, 0.459F, 0.96F * alpha};
    return {0.000F, 0.000F, 0.000F, 0.0F};
}

/// Search modal panel background.
[[nodiscard]] inline CHyprColor searchPanelColor() noexcept {
    return {0.058F, 0.063F, 0.082F, 0.97F};
}

/// Search text-input field fill.
[[nodiscard]] inline CHyprColor searchInputColor() noexcept {
    return {0.108F, 0.115F, 0.144F, 0.98F};
}

/// Search result row fill.  `alpha` is the animation multiplier.
[[nodiscard]] inline CHyprColor searchRowFill(bool selected, float alpha) noexcept {
    if (selected)
        return {0.178F, 0.160F, 0.115F, 0.98F * alpha};
    return {0.092F, 0.099F, 0.125F, 0.82F * alpha};
}

/// Accent bar colour for the selected search row (amber/gold).
[[nodiscard]] inline CHyprColor searchRowAccent() noexcept {
    return {0.980F, 0.740F, 0.280F, 1.0F};
}

[[nodiscard]] inline CHyprColor searchBadgeSpace() noexcept {
    return {0.105F, 0.158F, 0.154F, 0.92F};
}

[[nodiscard]] inline CHyprColor searchBadgeWindow() noexcept {
    return {0.160F, 0.136F, 0.090F, 0.92F};
}

} // namespace hypr_radiant::Theme
