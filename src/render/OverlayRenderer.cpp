#include <hypr-radiant/overview/OverlayGeometry.hpp>
#include <hypr-radiant/render/OverlayRenderer.hpp>
#include <hypr-radiant/HyprlandCompat.hpp>
#include <hypr-radiant/overview/AppIdentity.hpp>
#include <hypr-radiant/Log.hpp>
#include <hypr-radiant/overview/SearchPanelGeometry.hpp>
#include <hypr-radiant/overview/StageTransform.hpp>
#include <hypr-radiant/render/Theme.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hypr_radiant {
namespace {

// Multiplier applied to the configured animation duration for the workspace depth push.
constexpr auto WORKSPACE_PUSH_SCALE        = 1.3;
constexpr auto RIBBON_WORKSPACE_PUSH_SCALE = 0.68;
constexpr auto RIBBON_DURATION_CAP_MS       = 160;
// Band along the bottom edge the hint dock counts as its own. The pointer has to leave this
// entirely before the dock retracts, so a small twitch over the dock does not dismiss it.
constexpr auto DOCK_BAND_HEIGHT = 92.0;
// Short enough to feel attached to the pointer rather than played back at it.
constexpr auto CLOSE_REVEAL_MS = 140;
constexpr auto CLOSE_HOT_MS    = 110;

double easedProgress(double value) {
    const auto clamped = std::clamp(value, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

struct CarouselMotion {
    double startScale;
    double verticalTravel;
    double wallVerticalTravel;
    double horizontalTravel;
    double staggerSpan;
    double stageStartScale;
    double stageVerticalTravel;
    double stageHorizontalTravel;
    bool   alternateHorizontal;
    bool   sweepFromEdges;
    bool   stageAlternateHorizontal;
};

constexpr CarouselMotion carouselMotion(MotionPreference preference) noexcept {
    switch (preference) {
    case MotionPreference::Quattro:
        return {.startScale = 0.78, .verticalTravel = 0.0, .wallVerticalTravel = 0.0, .horizontalTravel = 0.0, .staggerSpan = 0.045, .stageStartScale = 1.20, .stageVerticalTravel = 0.0, .stageHorizontalTravel = 0.0, .alternateHorizontal = false, .sweepFromEdges = false, .stageAlternateHorizontal = false};
    case MotionPreference::Cyberpunk:
        return {.startScale = 1.0, .verticalTravel = 8.0, .wallVerticalTravel = 8.0, .horizontalTravel = 64.0, .staggerSpan = 0.025, .stageStartScale = 1.0, .stageVerticalTravel = 0.0, .stageHorizontalTravel = 72.0, .alternateHorizontal = true, .sweepFromEdges = false, .stageAlternateHorizontal = true};
    case MotionPreference::Tron:
        return {.startScale = 0.97, .verticalTravel = 10.0, .wallVerticalTravel = 10.0, .horizontalTravel = 220.0, .staggerSpan = 0.09, .stageStartScale = 1.0, .stageVerticalTravel = 10.0, .stageHorizontalTravel = 220.0, .alternateHorizontal = false, .sweepFromEdges = true, .stageAlternateHorizontal = false};
    case MotionPreference::Elegant:
        return {.startScale = 0.88, .verticalTravel = 54.0, .wallVerticalTravel = 54.0, .horizontalTravel = 0.0, .staggerSpan = 0.18, .stageStartScale = 0.90, .stageVerticalTravel = 54.0, .stageHorizontalTravel = 0.0, .alternateHorizontal = false, .sweepFromEdges = false, .stageAlternateHorizontal = false};
    case MotionPreference::Off:
        return {.startScale = 1.0, .verticalTravel = 0.0, .wallVerticalTravel = 0.0, .horizontalTravel = 0.0, .staggerSpan = 0.0, .stageStartScale = 1.0, .stageVerticalTravel = 0.0, .stageHorizontalTravel = 0.0, .alternateHorizontal = false, .sweepFromEdges = false, .stageAlternateHorizontal = false};
    case MotionPreference::FollowConfig:
    case MotionPreference::Reduced:
        // The Default composition intentionally matches Radiant's original card entrance exactly.
        return {.startScale = 0.94, .verticalTravel = 12.0, .wallVerticalTravel = 28.0, .horizontalTravel = 0.0, .staggerSpan = 0.13, .stageStartScale = 1.08, .stageVerticalTravel = 0.0, .stageHorizontalTravel = 0.0, .alternateHorizontal = false, .sweepFromEdges = false, .stageAlternateHorizontal = false};
    }
    return {.startScale = 0.94, .verticalTravel = 12.0, .wallVerticalTravel = 28.0, .horizontalTravel = 0.0, .staggerSpan = 0.13, .stageStartScale = 1.08, .stageVerticalTravel = 0.0, .stageHorizontalTravel = 0.0, .alternateHorizontal = false, .sweepFromEdges = false, .stageAlternateHorizontal = false};
}

CBox boxFor(const LayoutRect& rect) {
    return CBox{std::round(rect.x), std::round(rect.y), std::round(rect.width), std::round(rect.height)};
}

CBox insetBox(const CBox& box, double amount) {
    return CBox{
        box.x + amount,
        box.y + amount,
        std::max(0.0, box.w - amount * 2.0),
        std::max(0.0, box.h - amount * 2.0),
    };
}

void drawRect(const CBox& box, CHyprColor color, const CRegion& damage, int round = 0, bool blur = false) {
    if (!g_pHyprRenderer || box.w <= 0.0 || box.h <= 0.0)
        return;

    (void)damage;

    CRectPassElement::SRectData data;
    data.box   = box;
    data.color = color;
    data.round = round;
    data.blur  = blur;
    data.blurA = color.a;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
}

// Single-stop border. Folds in the g_pHyprRenderer guard that four of the seven old call sites were
// missing, so a border can never be the thing that dereferences a null renderer.
void drawBorder(const CBox& box, CHyprColor color, int round, int borderSize) {
    if (!g_pHyprRenderer || box.w <= 0.0 || box.h <= 0.0)
        return;

    CBorderPassElement::SBorderData border;
    border.box        = box;
    border.grad1      = Config::CGradientValueData{color};
    border.a          = static_cast<float>(color.a);
    border.round      = round;
    border.borderSize = borderSize;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
}

// Two-stop gradient border at an angle — Hyprland's own border idiom, used by the dock rim.
void drawBorder(const CBox& box, CHyprColor from, CHyprColor to, float angle, float alpha, int round, int borderSize) {
    if (!g_pHyprRenderer || box.w <= 0.0 || box.h <= 0.0)
        return;

    CBorderPassElement::SBorderData border;
    border.box        = box;
    border.grad1      = Config::CGradientValueData{std::vector<CHyprColor>{from, to}, angle};
    border.a          = alpha;
    border.round      = round;
    border.borderSize = borderSize;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
}

void drawBorder(const CBox& box, const BorderGradient& gradient, float alpha, int round, int borderSize) {
    if (!g_pHyprRenderer || box.w <= 0.0 || box.h <= 0.0 || gradient.stops.empty())
        return;

    std::vector<CHyprColor> colors;
    colors.reserve(gradient.stops.size());
    for (const auto& stop : gradient.stops)
        colors.emplace_back(stop.red, stop.green, stop.blue, stop.alpha);

    CBorderPassElement::SBorderData border;
    border.box        = box;
    border.grad1      = Config::CGradientValueData{std::move(colors), gradient.angle};
    border.a          = alpha;
    border.round      = round;
    border.borderSize = borderSize;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
}

CHyprColor withAlpha(CHyprColor color, double multiplier) {
    color.a *= multiplier;
    return color;
}

void drawSignalLock(const LayoutRect& rect, double progress, CHyprColor color, double alpha, const CRegion& damage) {
    constexpr auto PI_VALUE = 3.14159265358979323846;
    const auto     clamped  = std::clamp(progress, 0.0, 1.0);
    const auto     strength = std::sin(PI_VALUE * clamped);
    const auto     settled  = easedProgress(clamped);
    if (alpha <= 0.001)
        return;

    if (strength > 0.001) {
        const auto line       = boxFor(signalSweepRect(rect, clamped));
        const auto brightSpan = std::clamp(line.w * 0.28, 18.0, 88.0);
        // One quiet acquisition sweep, bright only at its centre. The faint full-width carrier
        // keeps it legible over both live previews and empty workspace glass.
        drawRect(line, withAlpha(color, alpha * 0.28 * strength), damage, 1);
        drawRect(CBox{line.x + centered(line.w, brightSpan), line.y, brightSpan, line.h},
            withAlpha(color, alpha * 0.86 * strength), damage, 1);

        // A single packet traces the card perimeter clockwise. It is intentionally short: a full
        // animated border would overpower the theme instead of reading like terminal telemetry.
        const auto traceBox    = boxFor(scaledAroundCenter(rect, std::lerp(1.035, 1.0, settled)));
        const auto side        = std::min(3, static_cast<int>(clamped * 4.0));
        const auto sidePhase   = clamped >= 1.0 ? 1.0 : clamped * 4.0 - static_cast<double>(side);
        const auto horizontal  = std::clamp(traceBox.w * 0.16, 12.0, 58.0);
        const auto vertical    = std::clamp(traceBox.h * 0.16, 10.0, 42.0);
        CBox packet;
        if (side == 0)
            packet = {traceBox.x + (traceBox.w - horizontal) * sidePhase, traceBox.y, horizontal, 2.0};
        else if (side == 1)
            packet = {traceBox.x + traceBox.w - 2.0, traceBox.y + (traceBox.h - vertical) * sidePhase, 2.0, vertical};
        else if (side == 2)
            packet = {traceBox.x + (traceBox.w - horizontal) * (1.0 - sidePhase), traceBox.y + traceBox.h - 2.0, horizontal, 2.0};
        else
            packet = {traceBox.x, traceBox.y + (traceBox.h - vertical) * (1.0 - sidePhase), 2.0, vertical};
        drawRect(packet, withAlpha(color, alpha * 0.92 * strength), damage, 1);
        drawBorder(traceBox, withAlpha(color, alpha * 0.10 * strength), 4, 1);
    }

    // The moving trace resolves into compact corner locks rather than a persistent glowing frame.
    // They converge from outside the card, giving both selection and a drag destination a stable
    // end state while preserving the existing Omarchy glass treatment.
    const auto locked       = scaledAroundCenter(rect, std::lerp(1.045, 1.0, settled));
    const auto lockBox      = boxFor(locked);
    const auto cornerLength = std::clamp(std::min(lockBox.w, lockBox.h) * 0.13, 8.0, 22.0);
    const auto cornerAlpha  = alpha * 0.52 * settled;
    constexpr auto weight  = 2.0;
    constexpr auto inset   = 5.0;
    const auto left         = lockBox.x + inset;
    const auto right        = lockBox.x + lockBox.w - inset;
    const auto top          = lockBox.y + inset;
    const auto bottom       = lockBox.y + lockBox.h - inset;
    drawRect({left, top, cornerLength, weight}, withAlpha(color, cornerAlpha), damage, 1);
    drawRect({left, top, weight, cornerLength}, withAlpha(color, cornerAlpha), damage, 1);
    drawRect({right - cornerLength, top, cornerLength, weight}, withAlpha(color, cornerAlpha), damage, 1);
    drawRect({right - weight, top, weight, cornerLength}, withAlpha(color, cornerAlpha), damage, 1);
    drawRect({left, bottom - weight, cornerLength, weight}, withAlpha(color, cornerAlpha), damage, 1);
    drawRect({left, bottom - cornerLength, weight, cornerLength}, withAlpha(color, cornerAlpha), damage, 1);
    drawRect({right - cornerLength, bottom - weight, cornerLength, weight}, withAlpha(color, cornerAlpha), damage, 1);
    drawRect({right - weight, bottom - cornerLength, weight, cornerLength}, withAlpha(color, cornerAlpha), damage, 1);
}

CHyprColor tintedSurface(CHyprColor surface, CHyprColor tint, double amount) {
    const auto mix = static_cast<float>(std::clamp(amount, 0.0, 1.0));
    surface.r = std::lerp(surface.r, tint.r, mix);
    surface.g = std::lerp(surface.g, tint.g, mix);
    surface.b = std::lerp(surface.b, tint.b, mix);
    return surface;
}

const MonitorSnapshot* findMonitorSnapshot(const RadiantState& state, std::int64_t id) {
    const auto it = std::ranges::find(state.monitors, id, &MonitorSnapshot::id);
    return it == state.monitors.end() ? nullptr : &*it;
}

CBox fillBoxForAspect(const CBox& box, double sourceWidth, double sourceHeight) {
    if (box.w <= 0.0 || box.h <= 0.0 || sourceWidth <= 0.0 || sourceHeight <= 0.0)
        return box;

    const auto sourceAspect = sourceWidth / sourceHeight;
    const auto boxAspect    = box.w / box.h;

    if (sourceAspect > boxAspect) {
        const auto width = box.h * sourceAspect;
        return CBox{box.x - centered(width, box.w), box.y, width, box.h};
    }

    const auto height = box.w / sourceAspect;
    return CBox{box.x, box.y - centered(height, box.h), box.w, height};
}

bool targetInFrame(const WorkspaceWallFrame& frame, OverviewTarget target) {
    if (target.type == OverviewTargetType::None)
        return false;

    for (const auto& workspace : frame.workspaces) {
        if (target.type == OverviewTargetType::Workspace && workspace.workspaceId == target.workspaceId)
            return true;

        for (const auto& window : workspace.windows) {
            if ((target.type == OverviewTargetType::Window || target.type == OverviewTargetType::Application) &&
                window.stableId == target.windowId)
                return true;
        }
    }

    for (const auto& window : frame.stage.windows) {
        if ((target.type == OverviewTargetType::Window || target.type == OverviewTargetType::Application) &&
            window.stableId == target.windowId)
            return true;
    }

    return false;
}

PHLWINDOW findLiveWindow(std::uint64_t stableId) {
    if (!g_pCompositor)
        return nullptr;

    const auto& windows = HyprlandCompat::windows();
    const auto it = std::ranges::find_if(windows,
        [stableId](const PHLWINDOW& window) { return window && window->m_stableID == stableId && window->m_isMapped; });
    return it == windows.end() ? nullptr : *it;
}

SP<Render::ITexture> currentSurfaceTexture(const SP<CWLSurfaceResource>& surface) {
    if (!surface)
        return nullptr;

    if (surface->m_current.texture && surface->m_current.texture->ok())
        return surface->m_current.texture;

    if (surface->m_current.buffer && surface->m_current.buffer->m_texture && surface->m_current.buffer->m_texture->ok())
        return surface->m_current.buffer->m_texture;

    return nullptr;
}

RadiantSize renderSizeForMonitor(const PHLMONITOR& monitor) {
    return {
        .width  = std::max(1.0, monitor->m_transformedSize.x),
        .height = std::max(1.0, monitor->m_transformedSize.y),
    };
}

LayoutRect globalBoundsForMonitor(const PHLMONITOR& monitor) {
    return {
        .x      = monitor->m_position.x,
        .y      = monitor->m_position.y,
        .width  = std::max(1.0, monitor->m_size.x),
        .height = std::max(1.0, monitor->m_size.y),
    };
}

MonitorSnapshot snapshotForCurrentMonitor(const PHLMONITOR& monitor) {
    MonitorSnapshot snapshot;
    snapshot.id       = monitor->m_id;
    snapshot.name     = monitor->m_name;
    snapshot.geometry = {
        .position = {.x = monitor->m_position.x, .y = monitor->m_position.y},
        .size     = {.width = monitor->m_size.x, .height = monitor->m_size.y},
    };

    if (monitor->m_activeWorkspace) {
        snapshot.activeWorkspaceId   = monitor->m_activeWorkspace->m_id;
        snapshot.activeWorkspaceName = monitor->m_activeWorkspace->m_name;
    }

    return snapshot;
}

WorkspaceWallOptions layoutOptionsFor(LayoutMode mode, std::int64_t previewWorkspaceId, OverviewMode overviewMode, const std::string& applicationFilter) {
    if (mode == LayoutMode::WorkspaceWall)
        return {};

    if (mode == LayoutMode::Carousel) {
        return WorkspaceWallOptions{
            .minimumWorkspaceSlots = 0,
            .outerPadding          = 112.0,
            .cardGap               = 24.0,
            .windowGap             = 10.0,
            .windowInset           = 22.0,
            .focusedStage          = false,
            .carousel              = true,
            .previewWorkspaceId    = previewWorkspaceId,
            .mode                  = overviewMode,
            .applicationFilter     = applicationFilter,
        };
    }

    if (mode == LayoutMode::Ribbon) {
        return WorkspaceWallOptions{
            .minimumWorkspaceSlots = 0,
            .outerPadding          = 112.0,
            .cardGap               = 24.0,
            .windowGap             = 10.0,
            .windowInset           = 22.0,
            .focusedStage          = false,
            .carousel              = true,
            .ribbon                = true,
            .previewWorkspaceId    = previewWorkspaceId,
            .mode                  = overviewMode,
            .applicationFilter     = applicationFilter,
        };
    }

    return WorkspaceWallOptions{
        .minimumWorkspaceSlots = 0,
        .outerPadding          = 48.0,
        .cardGap               = 20.0,
        .windowGap             = 8.0,
        .windowInset           = 16.0,
        .focusedStage          = true,
        .previewWorkspaceId    = previewWorkspaceId,
        .mode                  = overviewMode,
        .applicationFilter     = applicationFilter,
    };
}

} // namespace

OverlayRenderer::OverlayRenderer(RadiantConfig& config, PreferencesStore& preferences) :
    m_config(config), m_preferences(preferences), m_labels(config) {}

void OverlayRenderer::refreshChromeStyle() {
    const auto preset = effectiveChromePreset();
    m_chrome = resolveChromeStyle({
        .preset = preset,
        .roundingOverride = m_config.roundingOverride(),
        .borderSizeOverride = m_config.borderSizeOverride(),
        .borderColorOverride = m_config.borderColorOverride(),
        .effects = m_config.effectsMode(),
        .native = preset == ChromePreset::Native ? m_decoration.read() : std::nullopt,
    });
    if (m_chrome.nativeUnavailable && !m_nativeWarningIssued) {
        log::warn("native chrome is unavailable; using the flat preset for this session");
        m_nativeWarningIssued = true;
    }
}

void OverlayRenderer::drawChromeRect(
    const CBox& box, CHyprColor color, const CRegion& damage, int radiantRound, bool blur) const {
    drawRect(box, color, damage, m_chrome.radius(radiantRound), blur && m_chrome.effects);
}

void OverlayRenderer::drawSelectedBorder(const CBox& box, CHyprColor fallback, int radiantRound, int radiantWidth, int outset) const {
    const auto round = m_chrome.rounding ? m_chrome.radius(radiantRound, outset) : radiantRound;
    if (m_chrome.selectedBorder) {
        drawBorder(box, *m_chrome.selectedBorder, static_cast<float>(fallback.a),
            round, m_chrome.borderWidth(radiantWidth));
        return;
    }
    drawBorder(box, fallback, round, m_chrome.borderWidth(radiantWidth));
}

void OverlayRenderer::drawInactiveBorder(const CBox& box, CHyprColor fallback, int radiantRound, int radiantWidth, int outset) const {
    const auto round = m_chrome.rounding ? m_chrome.radius(radiantRound, outset) : radiantRound;
    if (m_chrome.inactiveBorder) {
        drawBorder(box, *m_chrome.inactiveBorder, static_cast<float>(fallback.a),
            round, m_chrome.borderWidth(radiantWidth));
        return;
    }
    drawBorder(box, fallback, round, m_chrome.borderWidth(radiantWidth));
}

void OverlayRenderer::install() {
    if (!Event::bus())
        throw std::runtime_error{"hypr-radiant: Hyprland event bus is not available"};

    m_renderStageListener = Event::bus()->m_events.render.stage.listen([this](eRenderStage stage) { onRenderStage(stage); });
    m_monitorLayoutListener = Event::bus()->m_events.monitor.layoutChanged.listen([this]() {
        if (!m_animation.targetVisible() && !m_animation.renderable())
            return;

        rebuildFrames();
        damageAllMonitors();
    });
}

void OverlayRenderer::uninstall() {
    hideImmediate();
    m_monitorLayoutListener.reset();
    m_renderStageListener.reset();
}

void OverlayRenderer::beginSession(RadiantState state, OverviewMode mode, std::string applicationFilter, int stageDurationMs,
    const std::function<OverviewTarget(const WorkspaceWallFrame&)>& selectInitial) {
    applyMotionProfile();
    m_nativeWarningIssued = false;
    refreshChromeStyle();
    m_mode              = mode;
    m_applicationFilter = std::move(applicationFilter);
    m_state             = std::move(state);
    m_preferencesVisible = false;
    m_preferencesMonitorId = -1;
    resetPointerInteraction();
    m_shelfKeyboardRevealed = false;
    m_previousFrames.clear();
    clearSearch();
    rebuildFrames();

    // The else branch matters: without it an opening session inherits the previous one's monitor id
    // and selected target, which showAppExpose used to do because it was a hand-copied variant.
    if (const auto* frame = activeMonitorFrame()) {
        m_selectedFrameMonitorId = frame->monitorId;
        m_selectedTarget         = selectInitial(*frame);
    } else {
        m_selectedFrameMonitorId = -1;
        m_selectedTarget         = {};
    }

    m_labels.clear();
    m_shelfTransition.hideImmediate();
    m_dockTransition.hideImmediate();
    m_dragSettleTransition.hideImmediate();
    m_dragSettle = {};
    releaseHoverAffordances();
    m_animation.animateTo(true, effectiveAnimationDurationMs());
    normalizeShelfVisibility();
    m_stageTransitionMonitorId = -1;
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, stageDurationMs);
    animateSelection();
    damageAllMonitors();
}

void OverlayRenderer::show(RadiantState state) {
    beginSession(std::move(state), defaultOverviewMode(), {},
        std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * 0.78))),
        [this](const WorkspaceWallFrame& frame) { return m_hitTester.initialSelection(frame); });
}

void OverlayRenderer::showAppExpose(RadiantState state, std::string applicationClass) {
    beginSession(std::move(state), OverviewMode::AppExpose, std::move(applicationClass), effectiveAnimationDurationMs(),
        [this](const WorkspaceWallFrame& frame) -> OverviewTarget {
            // Exposé opens on the first matching window rather than the rail.
            if (!frame.stage.windows.empty()) {
                const auto& window = frame.stage.windows.front();
                return {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId};
            }
            return m_hitTester.initialSelection(frame);
        });
}

void OverlayRenderer::toggle(RadiantState state) {
    if (m_animation.targetVisible()) {
        m_state = std::move(state);
        rebuildFrames();
        clearSearch();
        m_labels.clear();
        releaseHoverAffordances();
        m_animation.animateTo(false, std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * 0.67))));
        damageAllMonitors();
        return;
    }

    show(std::move(state));
}

void OverlayRenderer::moveSelection(NavigationDirection direction) {
    if (m_preferencesVisible) {
        static constexpr std::array stageControls{
            PreferenceControl::WorkspaceView,
            PreferenceControl::WindowView,
            PreferenceControl::Shelf,
            PreferenceControl::Motion,
            PreferenceControl::Chrome,
            PreferenceControl::NativeTheme,
            PreferenceControl::AppExpose,
        };
        static constexpr std::array globalControls{
            PreferenceControl::WorkspaceView,
            PreferenceControl::Motion,
            PreferenceControl::Chrome,
            PreferenceControl::NativeTheme,
            PreferenceControl::AppExpose,
        };
        const auto controls = effectiveLayoutMode() == LayoutMode::Stage ?
            std::span<const PreferenceControl>{stageControls} : std::span<const PreferenceControl>{globalControls};
        const auto current = std::ranges::find(controls, m_selectedPreference);
        auto index = current == controls.end() ? std::size_t{0} : static_cast<std::size_t>(std::distance(controls.begin(), current));
        if (direction == NavigationDirection::Up)
            index = index == 0 ? controls.size() - 1 : index - 1;
        else if (direction == NavigationDirection::Down)
            index = (index + 1) % controls.size();
        else if (m_selectedPreference != PreferenceControl::AppExpose)
            (void)applyPreference(m_selectedPreference, -1, direction == NavigationDirection::Left ? -1 : 1);
        m_selectedPreference = controls[index];
        damageMonitorById(m_preferencesMonitorId);
        return;
    }

    if (m_searchActive) {
        moveSearchSelection(direction);
        return;
    }

    const auto* frame = frameForSelectedTarget();
    if (!frame)
        return;

    const auto previousTarget = m_selectedTarget;
    const auto previousWorkspace = m_selectedTarget.workspaceId;
    // rebuildFrames() clears m_frames, so nothing may read through `frame` past that point.
    const auto frameMonitorId = frame->monitorId;
    const auto spatialWindows = m_config.windowNavigation() == WindowNavigation::Spatial;
    m_selectedTarget = m_hitTester.moveSelection(*frame, m_selectedTarget, direction,
        {.spatialWindows = spatialWindows});
    m_selectedFrameMonitorId = frameMonitorId;
    if (effectiveLayoutMode() == LayoutMode::Stage && spatialWindows && effectiveShelfMode() == ShelfMode::Auto) {
        if (previousTarget.type == OverviewTargetType::Window && m_selectedTarget.type == OverviewTargetType::Workspace &&
            !m_shelfTransition.targetVisible()) {
            setWorkspaceShelfVisible(true);
            m_shelfKeyboardRevealed = true;
        } else if (previousTarget.type == OverviewTargetType::Workspace && m_selectedTarget.type == OverviewTargetType::Window &&
            m_shelfKeyboardRevealed) {
            setWorkspaceShelfVisible(false);
            m_shelfKeyboardRevealed = false;
        }
    }
    if (!sameTarget(previousTarget, m_selectedTarget))
        animateSelection();
    if ((effectiveLayoutMode() == LayoutMode::Stage || effectiveLayoutMode() == LayoutMode::Carousel ||
            effectiveLayoutMode() == LayoutMode::Ribbon) &&
        m_selectedTarget.workspaceId != previousWorkspace) {
        m_previousFrames = m_frames;
        rebuildFrames();
        m_stageTransitionMonitorId = frameMonitorId;
        m_stageTransition.hideImmediate();
        // Ribbon snaps like the theme picker; the other layouts keep their roomier depth push.
        const auto pushScale = effectiveLayoutMode() == LayoutMode::Ribbon ? RIBBON_WORKSPACE_PUSH_SCALE : WORKSPACE_PUSH_SCALE;
        m_stageTransition.animateTo(true,
            std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * pushScale))));
    }
    damageMonitorById(frameMonitorId);
}

void OverlayRenderer::cycleWindow(int step) {
    if (m_searchActive || m_preferencesVisible)
        return;
    const auto* frame = frameForSelectedTarget();
    if (!frame)
        return;
    const auto target = m_hitTester.cycleWindow(*frame, m_selectedTarget, step);
    if (sameTarget(target, m_selectedTarget))
        return;
    m_selectedTarget = target;
    m_selectedFrameMonitorId = frame->monitorId;
    if (m_shelfKeyboardRevealed && target.type == OverviewTargetType::Window) {
        setWorkspaceShelfVisible(false);
        m_shelfKeyboardRevealed = false;
    }
    animateSelection();
    damageAllMonitors();
}

void OverlayRenderer::selectTargetAt(double x, double y) {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return;

    auto target = m_searchActive ? searchTargetAt(*frame, localX, localY) : effectiveLayoutMode() == LayoutMode::Stage ?
        m_hitTester.hitTestDisplayedStage(*frame, localX, localY, m_shelfTransition.value()) :
        m_hitTester.hitTest(*frame, localX, localY);
    // The close button is part of its window as far as selection goes: crossing onto it must not
    // drop the card's highlight, or the button would blink out from under the pointer.
    if (target.type == OverviewTargetType::CloseWindow)
        target.type = OverviewTargetType::Window;
    if (target.type == OverviewTargetType::None)
        return;

    if (frame->monitorId == m_selectedFrameMonitorId && sameTarget(m_selectedTarget, target))
        return;

    const auto previousWorkspace  = m_selectedTarget.workspaceId;
    const auto previousMonitorId  = m_selectedFrameMonitorId;
    // rebuildFrames() below clears m_frames, so `frame` must not be read past that point.
    const auto frameMonitorId     = frame->monitorId;
    const auto crossedMonitor     = previousMonitorId != -1 && previousMonitorId != frameMonitorId;
    m_selectedTarget = target;
    m_selectedFrameMonitorId = frameMonitorId;
    animateSelection();
    if (!m_searchActive && (effectiveLayoutMode() == LayoutMode::Stage || effectiveLayoutMode() == LayoutMode::Carousel || effectiveLayoutMode() == LayoutMode::Ribbon) &&
        target.workspaceId != previousWorkspace) {
        // A push still in flight on this monitor means the pointer is skimming the rail rather than
        // settling on a card. Restarting from zero for every card it crosses meant a fast sweep
        // across a long rail cancelled each push before it was visible, so the depth move played
        // far too fast or never appeared. Let the in-flight push run on toward the new selection
        // and keep the frames it started from, so the sweep reads as one continuous move.
        const auto pushInFlight = m_stageTransition.running() && m_stageTransitionMonitorId == frameMonitorId;
        if (!pushInFlight)
            m_previousFrames = m_frames;
        rebuildFrames();
        // Landing on another monitor always reports a different workspace, but that is the pointer
        // crossing screens, not a deliberate step through the rail. Replaying the push there made
        // every monitor hop look like the overlay reopening.
        if (!crossedMonitor && !pushInFlight) {
            m_stageTransitionMonitorId = frameMonitorId;
            m_stageTransition.hideImmediate();
            // Ribbon snaps like the theme picker; the other layouts keep their roomier depth push.
            const auto pushScale = effectiveLayoutMode() == LayoutMode::Ribbon ? RIBBON_WORKSPACE_PUSH_SCALE : WORKSPACE_PUSH_SCALE;
            m_stageTransition.animateTo(true,
                std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * pushScale))));
        }
    }
    // Hovering only repaints the monitor under the pointer, plus whichever monitor lost the
    // selection highlight; damaging every monitor made unrelated screens visibly re-render.
    damageMonitorById(frameMonitorId);
    if (previousMonitorId != -1 && previousMonitorId != frameMonitorId)
        damageMonitorById(previousMonitorId);
}

void OverlayRenderer::pointerMoved(double x, double y) {
    m_pointerPosition = {.x = x, .y = y};
    if (m_preferencesVisible) {
        const auto hit = preferenceControlAt(x, y);
        if (hit.control != PreferenceControl::None)
            m_selectedPreference = hit.control;
        setPointerCursorOverride(hit.control != PreferenceControl::None);
        damageMonitorById(m_preferencesMonitorId);
        return;
    }

    if (!m_searchActive) {
        double localX = x;
        double localY = y;
        if (const auto* frame = frameForPoint(x, y, localX, localY)) {
            constexpr auto revealEdge = 12.0;
            if (effectiveLayoutMode() == LayoutMode::Stage) {
                const auto shelfBottom     = frame->rail.bounds.y + frame->rail.bounds.height + 20.0;
                const auto insideShelfBand = localY <= shelfBottom;
                if (localY <= frame->bounds.y + revealEdge)
                    setWorkspaceShelfVisible(true);
                // Retract only when the pointer actually leaves the shelf. Hiding on any move below it
                // made a scroll-revealed shelf vanish the moment the pointer twitched, then reappear
                // once it reached the top edge.
                else if (!m_pointerDown && m_pointerInsideShelfBand && !insideShelfBand)
                    setWorkspaceShelfVisible(false);
                m_pointerInsideShelfBand = insideShelfBand;
            }

            // Mirror of the shelf at the other edge: the dock lives off-screen until the pointer
            // reaches the bottom, then retracts once it leaves the band it occupies.
            const auto frameBottom    = frame->bounds.y + frame->bounds.height;
            const auto insideDockBand = localY >= frameBottom - DOCK_BAND_HEIGHT;
            if (localY >= frameBottom - revealEdge)
                setHintDockVisible(true);
            else if (!m_pointerDown && m_pointerInsideDockBand && !insideDockBand)
                setHintDockVisible(false);
            m_pointerInsideDockBand = insideDockBand;
        }
    }
    updateCloseAffordance(x, y);
    if (!m_pointerDown) {
        selectTargetAt(x, y);
        return;
    }

    if (m_pointerDownTarget.type != OverviewTargetType::Window)
        return;

    const auto dx = x - m_pointerDownPosition.x;
    const auto dy = y - m_pointerDownPosition.y;
    if (!m_dragging && std::hypot(dx, dy) >= 8.0)
        beginDrag();

    if (!m_dragging)
        return;

    updateDropTarget(dropTargetFor(hitTest(x, y)));
    if (m_dragTarget.type != OverviewTargetType::None) {
        m_selectedTarget = m_dragTarget;
        m_selectedFrameMonitorId = m_dragTarget.monitorId;
    }
    damageAllMonitors();
}

PointerAction OverlayRenderer::pointerButton(bool pressed, double x, double y) {
    if (pressed) {
        m_pointerDown = true;
        m_dragging = false;
        m_pointerDownPosition = {.x = x, .y = y};
        m_pointerPosition = m_pointerDownPosition;
        if (m_preferencesVisible) {
            m_pointerDownPreference = preferenceControlAt(x, y);
            if (!pointerInsidePreferencesPanel(x, y)) {
                togglePreferences();
                resetPointerInteraction();
            }
            return {};
        }
        m_pointerDownTarget = hitTest(x, y);
        m_dragTarget = {};
        m_dropTargetTransition.hideImmediate();
        // A press dips the card under the pointer before it is known whether this becomes a click or
        // a drag, so the overview acknowledges the button going down instead of looking inert.
        if (m_pointerDownTarget.type == OverviewTargetType::Window) {
            m_pressTransition.hideImmediate();
            m_pressTransition.animateTo(true, dragDurationMs(0.30));
            damageMonitorById(m_pointerDownTarget.monitorId);
        }
        return {};
    }

    if (!m_pointerDown)
        return {};

    if (m_preferencesVisible) {
        const auto released = preferenceControlAt(x, y);
        const auto action = released.control != PreferenceControl::None && released == m_pointerDownPreference ? applyPreference(released.control, released.value) : PointerAction{};
        resetPointerInteraction();
        damageAllMonitors();
        return action;
    }

    // Capture the release before pointerMoved() is allowed to preview another workspace and rebuild
    // the rail. Otherwise the card can animate away from the release coordinate between press and
    // release, turning a deliberate workspace click into a selection-only hover.
    const auto stableReleasedTarget = hitTest(x, y);
    pointerMoved(x, y);
    PointerAction action;
    if (m_dragging && m_dragTarget.type != OverviewTargetType::None) {
        action = {
            .type = m_dragTarget.type == OverviewTargetType::NewWorkspace ? PointerActionType::CreateWorkspaceAndMoveWindow : PointerActionType::MoveWindow,
            .target = m_dragTarget,
            .windowId = m_pointerDownTarget.windowId,
        };
    } else if (!m_dragging) {
        const auto pointerTravel = std::hypot(
            x - m_pointerDownPosition.x,
            y - m_pointerDownPosition.y);
        if (m_pointerDownTarget.type == OverviewTargetType::Workspace && pointerTravel < 8.0) {
            action = {.type = PointerActionType::Activate, .target = m_pointerDownTarget};
            resetPointerInteraction();
            damageAllMonitors();
            return action;
        }

        const auto releasedTarget =
            m_pointerDownTarget.type == OverviewTargetType::Workspace && sameTarget(stableReleasedTarget, m_pointerDownTarget) ?
            stableReleasedTarget : hitTest(x, y);
        if (sameTarget(releasedTarget, m_pointerDownTarget)) {
            if (releasedTarget.windowId == m_closingWindowId) {
                resetPointerInteraction();
                damageAllMonitors();
                return {};
            }
            // Press and release both landed on the same close button, so this is a close rather
            // than an activation. Anything else falls through to focusing the target.
            action = releasedTarget.type == OverviewTargetType::CloseWindow
                ? PointerAction{.type = PointerActionType::CloseWindow, .target = releasedTarget, .windowId = releasedTarget.windowId}
                       : PointerAction{.type = PointerActionType::Activate, .target = releasedTarget};
        }
    }

    // Captured before the interaction is reset: the settle needs the card, the pointer and the drop
    // target that the reset is about to clear.
    if (m_dragging)
        beginDragSettle(x, y);

    resetPointerInteraction();
    damageAllMonitors();
    return action;
}

std::optional<std::chrono::milliseconds> OverlayRenderer::beginWindowClose(std::uint64_t windowId) {
    if (windowId == 0 || m_closingWindowId != 0 || !active())
        return std::nullopt;

    const auto frame = std::ranges::find_if(m_frames, [windowId](const WorkspaceWallFrame& candidate) {
        return std::ranges::any_of(candidate.stage.windows, [windowId](const WindowCard& window) {
            return window.stableId == windowId;
        });
    });
    if (frame == m_frames.end())
        return std::nullopt;

    const auto configuredDuration = m_config.animationDurationMs();
    auto       durationMs         = 0;
    if (configuredDuration > 0)
        durationMs = std::clamp(static_cast<int>(std::round(configuredDuration * 0.82)), 110, 220);

    m_closingWindowId        = windowId;
    m_closingWindowMonitorId = frame->monitorId;
    m_windowCloseTransition.setProgress(1.0, true);
    m_windowCloseTransition.animateTo(false, durationMs);
    releaseHoverAffordances();
    damageMonitorById(m_closingWindowMonitorId);
    return std::chrono::milliseconds{durationMs};
}

void OverlayRenderer::cancelWindowClose(std::uint64_t windowId) {
    if (windowId == 0 || windowId != m_closingWindowId)
        return;

    if (!active() || !findWindowCard(windowId)) {
        completeWindowClose(windowId);
        return;
    }

    const auto restoreDuration = std::max(80, static_cast<int>(std::round(m_config.animationDurationMs() * 0.55)));
    m_windowCloseTransition.animateTo(true, restoreDuration);
    damageMonitorById(m_closingWindowMonitorId);
}

void OverlayRenderer::completeWindowClose(std::uint64_t windowId) {
    if (windowId == 0 || windowId != m_closingWindowId)
        return;

    const auto monitorId = m_closingWindowMonitorId;
    m_windowCloseTransition.hideImmediate();
    m_closingWindowId        = 0;
    m_closingWindowMonitorId = -1;
    damageMonitorById(monitorId);
}

void OverlayRenderer::refresh(RadiantState state) {
    m_state = std::move(state);
    m_previousFrames = m_frames;
    rebuildFrames();
    m_stageTransitionMonitorId = -1;
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, effectiveAnimationDurationMs());
    animateSelection();
    m_labels.clear();
    damageAllMonitors();
}

void OverlayRenderer::beginGestureOpen(RadiantState state) {
    show(std::move(state));
    m_animation.setProgress(0.0, true);
    m_stageTransition.setProgress(0.0, true);
    damageAllMonitors();
}

void OverlayRenderer::setGestureProgress(bool opening, double progress) {
    const auto visibleProgress = opening ? progress : 1.0 - progress;
    m_animation.setProgress(visibleProgress, true);
    m_stageTransition.setProgress(visibleProgress, true);
    damageAllMonitors();
}

void OverlayRenderer::finishGesture(bool opening, bool commit) {
    const auto visible = opening ? commit : !commit;
    if (!visible)
        releaseHoverAffordances();
    m_animation.animateTo(visible, effectiveAnimationDurationMs());
    m_stageTransition.animateTo(visible, effectiveAnimationDurationMs());
    damageAllMonitors();
}

void OverlayRenderer::appendSearchChar(char value) {
    if (m_preferencesVisible)
        return;

    if (m_searchQuery.size() >= 64)
        return;

    if (!m_searchActive)
        beginSearch();

    m_searchQuery.push_back(value);
    rebuildSearchMatches();
    selectFirstSearchMatch();
    damageAllMonitors();
}

void OverlayRenderer::beginSearch() {
    if (m_searchActive || m_preferencesVisible)
        return;

    m_searchActive       = true;
    m_preSearchTarget    = m_selectedTarget;
    m_preSearchMonitorId = m_selectedFrameMonitorId;
    rebuildSearchMatches();
    selectFirstSearchMatch();
    damageAllMonitors();
}

void OverlayRenderer::backspaceSearch() {
    if (!m_searchActive || m_searchQuery.empty())
        return;

    m_searchQuery.pop_back();
    rebuildSearchMatches();
    selectFirstSearchMatch();
    damageAllMonitors();
}

void OverlayRenderer::clearSearchOrHide() {
    if (m_preferencesVisible) {
        togglePreferences();
        return;
    }

    if (m_searchActive) {
        clearSearch();
        m_selectedTarget         = m_preSearchTarget;
        m_selectedFrameMonitorId = m_preSearchMonitorId;
        if (m_selectedTarget.type == OverviewTargetType::None) {
            if (const auto* frame = activeMonitorFrame())
                m_selectedTarget = m_hitTester.initialSelection(*frame);
        }
        rebuildFrames();
        damageAllMonitors();
        return;
    }

    hideImmediate();
}

void OverlayRenderer::toggleGroupedMode() {
    if (effectiveLayoutMode() != LayoutMode::Stage || m_searchActive || m_preferencesVisible)
        return;

    switch (m_mode) {
    case OverviewMode::Spatial:
        m_mode = OverviewMode::Grouped;
        break;
    case OverviewMode::Grouped:
        m_mode = OverviewMode::Deck;
        break;
    case OverviewMode::Deck:
    case OverviewMode::AppExpose:
        m_mode = OverviewMode::Spatial;
        break;
    }
    m_applicationFilter.clear();
    m_previousFrames = m_frames;
    rebuildFrames();
    if (const auto* frame = frameForMonitor(m_selectedFrameMonitorId)) {
        if (!targetInFrame(*frame, m_selectedTarget))
            m_selectedTarget = m_hitTester.initialSelection(*frame);
    }
    m_stageTransitionMonitorId = -1;
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, effectiveAnimationDurationMs());
    animateSelection();
    m_labels.clear();
    damageAllMonitors();
}

void OverlayRenderer::togglePreferences() {
    if (!active() || m_searchActive)
        return;

    m_preferencesVisible = !m_preferencesVisible;
    if (m_preferencesVisible) {
        // Quattro swaps the active theme directory atomically. Re-read it at the point the panel
        // appears as well as when the overview opens, so Ctrl+, cannot retain colors from the theme
        // that happened to be active at session start.
        m_installedThemes = installedOmarchyThemes();
        m_config.refreshPalette(m_preferences.state().nativeTheme);
        m_labels.clear();
        if (m_preferencesMonitorId == -1) {
            if (const auto* frame = frameForSelectedTarget())
                m_preferencesMonitorId = frame->monitorId;
            else if (const auto* frame = activeMonitorFrame())
                m_preferencesMonitorId = frame->monitorId;
        }
        m_selectedPreference = PreferenceControl::WorkspaceView;
        m_shelfTransition.animateTo(false, effectiveAnimationDurationMs());
        m_dockTransition.animateTo(true, effectiveAnimationDurationMs());
        releaseHoverAffordances();
    } else {
        m_preferencesMonitorId = -1;
        m_pointerDownPreference = {};
        normalizeShelfVisibility();
    }
    damageAllMonitors();
}

PointerAction OverlayRenderer::activatePreference() {
    if (!m_preferencesVisible)
        return {};
    if (m_selectedPreference == PreferenceControl::AppExpose)
        return applyPreference(m_selectedPreference);

    // Arrows and pointer options save as they change. Enter confirms the value already on screen
    // and leaves settings; advancing again here made keyboard confirmation silently alter it.
    if (!m_preferences.save())
        log::warn("could not save preferences to {}", m_preferences.path().string());
    togglePreferences();
    return {};
}

void OverlayRenderer::setWorkspaceShelfVisible(bool visible, bool explicitRequest) {
    if (explicitRequest || !visible)
        m_shelfKeyboardRevealed = false;
    if (effectiveLayoutMode() != LayoutMode::Stage || !active() || (!explicitRequest && !shelfAutomationAllowed(visible)) ||
        m_shelfTransition.targetVisible() == visible)
        return;

    const auto duration = effectiveAnimationDurationMs();
    m_shelfTransition.animateTo(visible, duration == 0 ? 0 : std::max(90, static_cast<int>(std::round(duration * 0.82))));
    damageAllMonitors();
}

void OverlayRenderer::updateCloseAffordance(double x, double y) {
    std::uint64_t windowId = 0;
    auto          hot      = false;
    if (!m_dragging && !m_searchActive && !m_preferencesVisible && effectiveLayoutMode() == LayoutMode::Stage && active()) {
        const auto target = hitTest(x, y);
        if (target.type == OverviewTargetType::Window || target.type == OverviewTargetType::CloseWindow) {
            windowId = target.windowId;
            hot      = target.type == OverviewTargetType::CloseWindow;
        }
    }

    if (windowId != m_closeButtonWindowId) {
        const auto hadButton = m_closeButtonWindowId != 0;
        m_closeButtonWindowId = windowId;
        if (windowId == 0)
            m_closeButtonTransition.animateTo(false, CLOSE_REVEAL_MS);
        else if (!hadButton)
            m_closeButtonTransition.animateTo(true, CLOSE_REVEAL_MS);
        else
            // Card to card the affordance is already established, so it tracks the pointer at full
            // size rather than replaying its entrance on every neighbour.
            m_closeButtonTransition.setProgress(1.0, true);
        // The button lives on the monitor under the pointer, so only that screen needs repainting.
        damageMonitorById(m_selectedFrameMonitorId);
    }

    if (hot != m_closeButtonHot) {
        m_closeButtonHot = hot;
        m_closeButtonHotTransition.animateTo(hot, CLOSE_HOT_MS);
        damageMonitorById(m_selectedFrameMonitorId);
    }
    setPointerCursorOverride(hot);
}

void OverlayRenderer::setPointerCursorOverride(bool pointerCursor) {
    if (m_pointerCursorActive == pointerCursor || !g_pHyprRenderer)
        return;

    m_pointerCursorActive = pointerCursor;
    // Restoring by name rather than remembering the previous shape: the overlay owns the pointer
    // while it is up, and "left_ptr" is the default Hyprland itself falls back to, so closing while
    // hovering a button cannot strand the hand cursor on the desktop.
    g_pHyprRenderer->setCursorFromName(pointerCursor ? "pointer" : "left_ptr");
}

void OverlayRenderer::releaseHoverAffordances() {
    m_closeButtonTransition.hideImmediate();
    m_closeButtonHotTransition.hideImmediate();
    m_closeButtonWindowId = 0;
    m_closeButtonHot      = false;
    setPointerCursorOverride(false);
}

void OverlayRenderer::setHintDockVisible(bool visible) {
    if (!active() || m_dockTransition.targetVisible() == visible)
        return;

    const auto duration = effectiveAnimationDurationMs();
    m_dockTransition.animateTo(visible, duration == 0 ? 0 : std::max(90, static_cast<int>(std::round(duration * 0.82))));
    damageAllMonitors();
}

void OverlayRenderer::toggleWorkspaceShelf() {
    setWorkspaceShelfVisible(!m_shelfTransition.targetVisible(), true);
}

void OverlayRenderer::setWorkspaceShelfGestureProgress(bool revealing, double progress) {
    if (effectiveLayoutMode() != LayoutMode::Stage || !active() || !shelfAutomationAllowed(revealing))
        return;

    m_shelfTransition.setProgress(revealing ? progress : 1.0 - progress, revealing);
    damageAllMonitors();
}

void OverlayRenderer::finishWorkspaceShelfGesture(bool revealing, bool commit) {
    const auto visible = revealing ? commit : !commit;
    if (!shelfAutomationAllowed(visible)) {
        normalizeShelfVisibility();
        return;
    }
    const auto duration = effectiveAnimationDurationMs();
    m_shelfTransition.animateTo(visible, duration == 0 ? 0 : std::max(90, static_cast<int>(std::round(duration * 0.72))));
    damageAllMonitors();
}

void OverlayRenderer::hideImmediate() {
    const auto wasRenderable = m_animation.renderable();
    m_animation.hideImmediate();
    m_stageTransition.hideImmediate();
    m_selectionTransition.hideImmediate();
    m_windowCloseTransition.hideImmediate();
    m_shelfTransition.hideImmediate();
    m_shelfKeyboardRevealed = false;
    m_dockTransition.hideImmediate();
    m_dragSettleTransition.hideImmediate();
    m_dragSettle = {};
    releaseHoverAffordances();
    m_frames.clear();
    m_previousFrames.clear();
    m_frameBoundsByMonitor.clear();
    m_labels.clear();
    m_selectedTarget = {};
    m_selectedFrameMonitorId = -1;
    m_stageTransitionMonitorId = -1;
    m_closingWindowId        = 0;
    m_closingWindowMonitorId = -1;
    clearSearch();
    m_preferencesVisible = false;
    m_preferencesMonitorId = -1;
    resetPointerInteraction();

    if (wasRenderable)
        damageAllMonitors();
}

void OverlayRenderer::resetPointerInteraction() {
    m_pointerDownTarget = {};
    m_dragTarget = {};
    m_pointerDownPosition = {};
    m_pointerPosition = {};
    m_pointerDown = false;
    m_dragging = false;
    m_pointerDownPreference = {};
    // Dropped rather than animated out: both timelines are drawn against the card the press landed
    // on, and that card is exactly what this reset forgets. The settle keeps its own copy.
    m_pressTransition.hideImmediate();
    m_dragLiftTransition.hideImmediate();
    m_dropTargetTransition.hideImmediate();
}

int OverlayRenderer::dragDurationMs(double scale) const {
    const auto base = effectiveAnimationDurationMs();
    if (base <= 0)
        return 0;

    return std::max(1, static_cast<int>(std::round(base * scale)));
}

void OverlayRenderer::beginDrag() {
    m_dragging = true;
    // The card leaves its slot instead of teleporting to the cursor: the lift interpolates it from
    // where it sat to a pointer-anchored card, and the slot it vacated fades out behind it.
    m_pressTransition.animateTo(false, dragDurationMs(0.22));
    m_dragSettleTransition.hideImmediate();
    m_dragSettle = {};
    m_dragLiftTransition.hideImmediate();
    m_dragLiftTransition.animateTo(true, dragDurationMs(0.62));
}

OverviewTarget OverlayRenderer::dropTargetFor(OverviewTarget hit) const {
    // A workspace is mostly covered by the window cards sitting inside it, so a pointer over one of
    // those is still a pointer over that workspace. Reading the hit literally meant a drop only
    // landed in the gaps between cards, and released anywhere else it did nothing at all.
    const auto resolved = hit.type == OverviewTargetType::Window || hit.type == OverviewTargetType::CloseWindow
        ? OverviewTarget{.type = OverviewTargetType::Workspace, .workspaceId = hit.workspaceId, .monitorId = hit.monitorId}
                          : hit;

    if (resolved.type != OverviewTargetType::Workspace && resolved.type != OverviewTargetType::NewWorkspace)
        return {};

    // Released over the workspace the window already lives on: not a move. Reporting no destination
    // makes it a cancelled drag, so the card flies back into its slot instead of round-tripping
    // through the compositor to end up exactly where it started.
    if (resolved.type == OverviewTargetType::Workspace && resolved.workspaceId == m_pointerDownTarget.workspaceId)
        return {};

    return resolved;
}

void OverlayRenderer::updateDropTarget(OverviewTarget target) {
    // monitorId is part of the comparison even though sameTarget ignores it: the create-workspace
    // card carries the same id on every monitor, and the drop has to land on the one under the
    // pointer rather than the one the drag happened to start over.
    if (sameTarget(target, m_dragTarget) && target.monitorId == m_dragTarget.monitorId)
        return;

    m_dragTarget = target;
    if (m_dragTarget.type == OverviewTargetType::None) {
        m_dropTargetTransition.animateTo(false, dragDurationMs(0.30));
        return;
    }

    // Restarted rather than continued, so crossing from one workspace to the next re-plays the
    // highlight on the card the pointer just entered instead of leaving it mid-fade.
    m_dropTargetTransition.hideImmediate();
    m_dropTargetTransition.animateTo(true, dragDurationMs(0.78));
}

void OverlayRenderer::beginDragSettle(double x, double y) {
    const auto* source = findWindowCard(m_pointerDownTarget.windowId);
    double      localX = x;
    double      localY = y;
    const auto* frame  = frameForPoint(x, y, localX, localY);
    if (!source || !frame)
        return;

    const auto from = dragCardRect(source->rect, {.x = localX, .y = localY}, easedProgress(m_dragLiftTransition.value()));
    // Released over nothing droppable: shrink away where the card is, unless the wall can show it
    // falling back into the slot it came from. Stage cards are drawn through the shelf remap, so
    // their layout rect is not where they appear and cannot be flown back to.
    auto to = m_dragTarget.type == OverviewTargetType::None && effectiveLayoutMode() == LayoutMode::WorkspaceWall
        ? source->rect
               : scaledAroundCenter(from, 0.86);
    auto landed = false;
    if (m_dragTarget.type != OverviewTargetType::None) {
        if (const auto* workspace = findWorkspaceCard(m_dragTarget.workspaceId)) {
            to     = dragLandingRect(from, workspace->rect);
            landed = true;
        }
    }

    m_dragSettle = {.monitorId = frame->monitorId, .windowId = source->stableId, .from = from, .to = to};
    m_dragSettleTransition.setProgress(1.0, true);
    m_dragSettleTransition.animateTo(false, dragDurationMs(landed ? 0.78 : 0.56));
}

void OverlayRenderer::animateSelection() {
    m_selectionTransition.hideImmediate();
    m_selectionTransition.animateTo(true, std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * 0.92))));
}

bool OverlayRenderer::active() const noexcept {
    return m_animation.targetVisible();
}

bool OverlayRenderer::searchActive() const noexcept {
    return m_searchActive;
}

bool OverlayRenderer::preferencesVisible() const noexcept {
    return m_preferencesVisible;
}

bool OverlayRenderer::workspaceShelfVisible() const noexcept {
    return m_shelfTransition.targetVisible();
}

OverviewMode OverlayRenderer::mode() const noexcept {
    return m_mode;
}

OverviewTarget OverlayRenderer::selectedTarget() const noexcept {
    return m_selectedTarget;
}

OverviewTarget OverlayRenderer::hitTest(double x, double y) {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return {};

    if (m_searchActive)
        return searchTargetAt(*frame, localX, localY);
    if (effectiveLayoutMode() == LayoutMode::Stage)
        return m_hitTester.hitTestDisplayedStage(*frame, localX, localY, m_shelfTransition.value());
    return m_hitTester.hitTest(*frame, localX, localY);
}

void OverlayRenderer::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_LAST_MOMENT || !m_animation.renderable())
        return;

    // Session-lock surfaces are security-sensitive compositor UI. Never draw Radiant over them,
    // even during the tick before the plugin's lock guard clears its overlay state.
    if (g_pSessionLockManager && g_pSessionLockManager->isSessionLocked())
        return;

    if (m_closingWindowId != 0 && m_windowCloseTransition.targetVisible() && !m_windowCloseTransition.running())
        completeWindowClose(m_closingWindowId);

    const auto alpha = std::clamp(static_cast<float>(m_animation.value()), 0.0F, 1.0F);

    if (alpha > 0.001F)
        renderCurrentMonitor(alpha);

    // Keep scheduling frames while anything animates, but only for the monitors that actually change.
    // The open/close fade and the shelf/dock reveals are single shared timelines that touch every
    // frame, so they need every monitor; the stage push, selection highlight and close button are
    // monitor-local. Damaging all monitors for a one-screen hover was making unrelated screens
    // re-render every frame of a 140 ms button fade.
    // Drag affordances get every monitor: a card can be picked up on one screen and dropped on
    // another, so the lift, the destination highlight and the settle are never monitor-local.
    const auto dragAnimating = m_pressTransition.running() || m_dragLiftTransition.running() ||
        m_dropTargetTransition.running() || m_dragSettleTransition.running();

    if (m_animation.running() || m_shelfTransition.running() || m_dockTransition.running() || dragAnimating) {
        damageAllMonitors();
    } else {
        if (m_stageTransition.running()) {
            if (m_stageTransitionMonitorId == -1)
                damageAllMonitors();
            else
                damageMonitorById(m_stageTransitionMonitorId);
        }
        if (m_selectionTransition.running() || m_closeButtonTransition.running() || m_closeButtonHotTransition.running())
            damageMonitorById(m_selectedFrameMonitorId);
        if (m_windowCloseTransition.running())
            damageMonitorById(m_closingWindowMonitorId);
    }
}

void OverlayRenderer::renderCurrentMonitor(double alpha) {
    if (!g_pHyprRenderer || !g_pCompositor)
        return;

    const auto monitor = g_pHyprRenderer->renderData().pMonitor.lock();

    if (!HyprlandCompat::monitorExists(monitor))
        return;

    const auto width  = monitor->m_transformedSize.x;
    const auto height = monitor->m_transformedSize.y;

    if (width <= 0.0 || height <= 0.0)
        return;

    auto box = CBox{0, 0, width, height};
    const auto& damage = g_pHyprRenderer->renderData().damage;
    const auto backdropAlpha = alpha * m_config.opacity();

    // Frosted glass: a lifted step off the theme background rather than the background itself,
    // so the desktop stays faintly legible behind the overview instead of being crushed to black.
    // Both overview layouts follow the live Omarchy background. The old wall path used a fixed
    // green-black backdrop, so changing themes updated its accent but left most of the screen
    // behind in the previous design's palette.
    auto backdrop = surfaceColor(0.055F, effectiveLayoutMode() == LayoutMode::Stage ? 0.70 : 0.40);
    if (effectiveLayoutMode() == LayoutMode::WorkspaceWall || effectiveLayoutMode() == LayoutMode::Carousel ||
        effectiveLayoutMode() == LayoutMode::Ribbon)
        backdrop = tintedSurface(backdrop, resolvedAccentColor(), 0.08);
    backdrop.a *= backdropAlpha;
    drawChromeRect(box, backdrop, damage, 0, true);

    const auto* frame = frameForMonitor(monitor->m_id);
    if (!frame)
        return;

    const auto sizeChanged = std::abs(frame->bounds.width - width) > 1.0 || std::abs(frame->bounds.height - height) > 1.0;
    if (sizeChanged)
        return;

    renderFrame(*frame, alpha, damage);
    renderHintDock(*frame, alpha, resolvedAccentColor(), damage);
    renderPreferencesPanel(*frame, alpha, damage);
}

void OverlayRenderer::rebuildFrames() {
    m_frames.clear();
    m_frameBoundsByMonitor.clear();

    if (g_pCompositor) {
        for (const auto& monitor : HyprlandCompat::monitors()) {
            if (!HyprlandCompat::monitorExists(monitor))
                continue;

            auto snapshot = snapshotForCurrentMonitor(monitor);
            if (const auto* collected = findMonitorSnapshot(m_state, monitor->m_id)) {
                const auto liveGeometry = snapshot.geometry;
                snapshot                = *collected;
                snapshot.geometry       = liveGeometry;
            }

            const auto renderSize = renderSizeForMonitor(monitor);
            const auto previewWorkspaceId = snapshot.id == m_selectedFrameMonitorId && m_selectedTarget.workspaceId > 0 ?
                m_selectedTarget.workspaceId : snapshot.activeWorkspaceId;
            m_frameBoundsByMonitor[snapshot.id] = globalBoundsForMonitor(monitor);
            m_frames.push_back(m_layout.compute(m_state, snapshot, renderSize,
                scaledSpacing(layoutOptionsFor(effectiveLayoutMode(), previewWorkspaceId, m_mode, m_applicationFilter), m_config.spacing())));
        }
    }

    if (m_frames.empty()) {
        for (const auto& monitor : m_state.monitors) {
            const auto renderSize = RadiantSize{
                .width  = std::max(1.0, monitor.geometry.size.width),
                .height = std::max(1.0, monitor.geometry.size.height),
            };
            m_frameBoundsByMonitor[monitor.id] = {
                .x      = monitor.geometry.position.x,
                .y      = monitor.geometry.position.y,
                .width  = renderSize.width,
                .height = renderSize.height,
            };
            const auto previewWorkspaceId = monitor.id == m_selectedFrameMonitorId && m_selectedTarget.workspaceId > 0 ?
                m_selectedTarget.workspaceId : monitor.activeWorkspaceId;
            m_frames.push_back(m_layout.compute(m_state, monitor, renderSize,
                scaledSpacing(layoutOptionsFor(effectiveLayoutMode(), previewWorkspaceId, m_mode, m_applicationFilter), m_config.spacing())));
        }
    }

    if (m_selectedFrameMonitorId != -1 && !frameForMonitor(m_selectedFrameMonitorId))
        m_selectedFrameMonitorId = -1;

    rebuildSearchMatches();
}

void OverlayRenderer::rebuildSearchMatches() {
    m_searchMatches.clear();
    if (!m_searchActive)
        return;

    for (const auto& window : m_state.windows) {
        if (!window.mapped)
            continue;
        if (m_searchQuery.empty() || m_searchMatcher.matches(window.title, m_searchQuery) ||
            m_searchMatcher.matches(window.className, m_searchQuery))
            m_searchMatches.insert(window.stableId);
    }
}

void OverlayRenderer::selectFirstSearchMatch() {
    if (!m_searchActive)
        return;

    const auto targets = matchingSearchTargets();
    if (!targets.empty()) {
        m_selectedTarget = targets.front();
        if (targets.front().monitorId >= 0)
            m_selectedFrameMonitorId = targets.front().monitorId;
        else if (const auto* frame = frameForSelectedTarget())
            m_selectedFrameMonitorId = frame->monitorId;
        return;
    }

    m_selectedTarget = {};
}

std::vector<OverviewTarget> OverlayRenderer::matchingSearchTargets() const {
    std::vector<OverviewTarget> targets;
    for (const auto& suggestion : matchingSearchSuggestions())
        targets.push_back(suggestion.target);
    return targets;
}

std::vector<SearchSuggestion> OverlayRenderer::matchingSearchSuggestions() const {
    if (!m_searchActive)
        return {};
    return buildSearchSuggestions(m_state, m_searchQuery);
}

void OverlayRenderer::moveSearchSelection(NavigationDirection direction) {
    const auto targets = matchingSearchTargets();
    if (targets.empty())
        return;

    const auto current = std::ranges::find_if(targets, [this](OverviewTarget target) { return sameTarget(target, m_selectedTarget); });
    auto index = current == targets.end() ? 0 : static_cast<std::size_t>(std::distance(targets.begin(), current));

    if (direction == NavigationDirection::Left || direction == NavigationDirection::Up)
        index = index == 0 ? targets.size() - 1 : index - 1;
    else
        index = (index + 1) % targets.size();

    m_selectedTarget = targets[index];
    if (targets[index].monitorId >= 0)
        m_selectedFrameMonitorId = targets[index].monitorId;
    else if (const auto* frame = frameForSelectedTarget())
        m_selectedFrameMonitorId = frame->monitorId;
    damageAllMonitors();
}

void OverlayRenderer::clearSearch() {
    m_searchQuery.clear();
    m_searchMatches.clear();
    m_searchActive = false;
}

void OverlayRenderer::renderFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    if (effectiveLayoutMode() == LayoutMode::Stage) {
        renderStageFrame(frame, alpha, damage);
        return;
    }

    const auto searchActive = m_searchActive;
    const auto contentAlpha = searchActive ? alpha * 0.07 : alpha;
    const auto accent     = resolvedAccentColor();
    const auto foreground = m_config.foregroundColor();
    const auto accentLit  = tintedSurface(accent, foreground, 0.24);
    const auto entrance     = std::clamp(m_stageTransition.value(), 0.0, 1.0);
    const auto selectionProgress = std::clamp(m_selectionTransition.value(), 0.0, 1.0);
    const auto selection    = easedProgress(selectionProgress);
    // Typography follows the actual interactive progress, not the animation's eventual target.
    // Gesture-driven closes keep targetVisible() true until release, so a target-based curve left
    // labels fully present and then dropped their textures at the end. This delayed smoothstep
    // makes them fade continuously after the glass on reveal and before it on dismissal.
    const auto typographyFade = easedProgress((alpha - 0.08) / 0.72);
    const auto headingAlpha = contentAlpha * typographyFade;
    // Drag affordances resolve once per frame and are read by both loops below: the card being
    // carried, the slot it left behind and the workspace it would land on all move together.
    const auto dragLift = m_dragging ? easedProgress(m_dragLiftTransition.value()) : 0.0;
    const auto dropProgress = std::clamp(m_dropTargetTransition.value(), 0.0, 1.0);
    const auto dropGlow = easedProgress(dropProgress);
    const auto pressDip = m_pointerDown && !m_dragging && m_pointerDownTarget.type == OverviewTargetType::Window
        ? easedProgress(m_pressTransition.value())
                          : 0.0;
    const auto motion = m_preferences.state().motion;
    const auto motionSpec = carouselMotion(motion);
    std::optional<LayoutRect> draggedSlot;

    if (!frame.carousel) {
        auto contentLeft = frame.bounds.width;
        auto contentTop  = frame.bounds.height;
        auto hasContent  = false;
        for (const auto& workspace : frame.workspaces) {
            if (workspace.rect.width <= 0.0 || workspace.rect.height <= 0.0)
                continue;
            contentLeft = std::min(contentLeft, workspace.rect.x);
            contentTop  = std::min(contentTop, workspace.rect.y);
            hasContent  = true;
        }
        const auto titleX = hasContent ? std::max(38.0, contentLeft) : 46.0;
        const auto titleY = hasContent ? std::max(30.0, contentTop - 58.0) : 30.0;
        const auto headingY = titleY + (1.0 - entrance) * 10.0;
        m_labels.renderColored("WORKSPACES", titleX, headingY, std::max(1.0, frame.bounds.width * 0.45),
            Theme::titleSize(), accentLit, headingAlpha, damage);
    }

    const WorkspaceWallFrame* previousCarouselFrame = nullptr;
    if (frame.carousel) {
        const auto previous = std::ranges::find_if(m_previousFrames, [&frame](const WorkspaceWallFrame& candidate) {
            return candidate.monitorId == frame.monitorId && candidate.carousel;
        });
        if (previous != m_previousFrames.end())
            previousCarouselFrame = &*previous;
    }

    for (std::size_t workspaceIndex = 0; workspaceIndex < frame.workspaces.size(); ++workspaceIndex) {
        const auto& workspace = frame.workspaces[workspaceIndex];
        const auto ownsSelection = frame.monitorId == m_selectedFrameMonitorId &&
            m_selectedTarget.workspaceId == workspace.workspaceId;
        const auto workspaceSelected = ownsSelection &&
            (m_selectedTarget.type == OverviewTargetType::Workspace || m_selectedTarget.type == OverviewTargetType::NewWorkspace);
        const auto carouselFocused = frame.carousel && workspace.workspaceId == frame.previewWorkspaceId;
        const auto carouselThumbnail = frame.carousel && !carouselFocused;
        const auto ribbonFocused = frame.ribbon && carouselFocused;
        const auto ribbonBlade   = frame.ribbon && carouselThumbnail;
        const auto compact = carouselThumbnail ? workspace.rect.width <= 110.0 || workspace.rect.height <= 70.0 :
                                                  workspace.rect.width <= 140.0 || workspace.rect.height <= 120.0;
        const auto round        = ribbonBlade ? 2 : ribbonFocused ? 8 : compact ? 14 : 18;
        const auto staggerSpan  = motionSpec.staggerSpan;
        const auto stagger      = frame.workspaces.size() <= 1 ? 0.0 :
            static_cast<double>(workspaceIndex) / static_cast<double>(frame.workspaces.size() - 1) * staggerSpan;
        const auto cardEntrance = easedProgress((entrance - stagger) / std::max(0.01, 1.0 - stagger));
        const auto hoverLift    = ownsSelection ? selection : 0.0;
        // The workspace the carried card would land on rises to meet it, so the destination is
        // legible from the card's own position rather than only under the pointer.
        const auto dropTarget   = m_dragging && m_dragTarget.type != OverviewTargetType::None &&
            m_dragTarget.monitorId == frame.monitorId && m_dragTarget.workspaceId == workspace.workspaceId;
        const auto drop         = dropTarget ? dropGlow : 0.0;
        auto baseRect = workspace.rect;
        if (previousCarouselFrame && entrance < 1.0) {
            const auto previousWorkspace = std::ranges::lower_bound(
                previousCarouselFrame->workspaces, workspace.workspaceId, {}, &WorkspaceCard::workspaceId);
            if (previousWorkspace != previousCarouselFrame->workspaces.end())
                baseRect = interpolatedRect(previousWorkspace->rect, workspace.rect, entrance);
        }
        const auto startScale = motionSpec.startScale;
        auto       displayRect  = scaledAroundCenter(baseRect,
            std::lerp(startScale, 1.0, cardEntrance) * std::lerp(1.0, workspaceSelected ? 1.018 : 1.006, hoverLift) * std::lerp(1.0, 1.03, drop),
            -std::lerp(0.0, workspaceSelected ? 7.0 : 3.0, hoverLift) - 6.0 * drop);
        const auto unresolved = 1.0 - cardEntrance;
        auto verticalTravel = frame.carousel ? motionSpec.verticalTravel : motionSpec.wallVerticalTravel;
        if (!frame.carousel && (motion == MotionPreference::FollowConfig || motion == MotionPreference::Reduced))
            verticalTravel += static_cast<double>(workspaceIndex % 3) * 7.0;
        displayRect.y += unresolved * verticalTravel;
        if (motionSpec.alternateHorizontal) {
            const auto direction = workspaceIndex % 2 == 0 ? -1.0 : 1.0;
            displayRect.x += unresolved * motionSpec.horizontalTravel * direction;
        } else if (motionSpec.sweepFromEdges) {
            const auto cardCenter = workspace.rect.x + workspace.rect.width / 2.0;
            const auto screenCenter = frame.bounds.width / 2.0;
            const auto direction = std::abs(cardCenter - screenCenter) < 1.0 ? 1.0 : cardCenter < screenCenter ? -1.0
                                                                                                : 1.0;
            displayRect.x += unresolved * motionSpec.horizontalTravel * direction;
        }
        const auto workspaceBox = boxFor(displayRect);
        const auto cardAlpha    = contentAlpha * cardEntrance * (ribbonBlade ? 0.62 : carouselThumbnail ? 0.80 : 1.0);
        const auto detailAlpha  = cardAlpha * typographyFade;

        if (m_chrome.effects && (ownsSelection || workspace.active || carouselFocused) && !compact) {
            const auto glowStrength = carouselFocused ? 0.065 : workspaceSelected ? 0.075 * selection : workspace.active ? 0.032 : 0.02 * selection;
            const auto spread       = carouselFocused ? 10.0 : workspaceSelected ? 11.0 : 7.0;
            const auto glowBox = CBox{
                workspaceBox.x - spread,
                workspaceBox.y - spread,
                workspaceBox.w + spread * 2.0,
                workspaceBox.h + spread * 2.0,
            };
            drawRect(glowBox, withAlpha(accent, cardAlpha * glowStrength), damage, round + static_cast<int>(spread));
        }

        const auto shadowLift = workspaceSelected ? 9.0 * selection : ownsSelection ? 4.0 * selection : 0.0;
        if (m_chrome.effects && (!carouselThumbnail || ribbonBlade)) {
            drawRect(CBox{workspaceBox.x + 5.0, workspaceBox.y + 7.0 + shadowLift * 0.30, workspaceBox.w, workspaceBox.h},
                withAlpha(Theme::shadowColor(), cardAlpha * (ribbonBlade ? 0.50 : 0.34 + hoverLift * 0.12)), damage, round + 2);
        }

        const auto surfaceLift = workspace.createTarget ? 0.075F : workspace.empty ? 0.085F :
            carouselFocused ? 0.155F : workspaceSelected ? static_cast<float>(0.145 + selection * 0.035) :
            workspace.active ? 0.13F : ownsSelection ? 0.125F : 0.105F;
        auto cardSurface = surfaceColor(surfaceLift, cardAlpha * (workspace.empty ? 0.64 : 0.74));
        cardSurface = tintedSurface(cardSurface, accent,
            carouselFocused ? 0.16 : workspaceSelected ? 0.14 + selection * 0.08 : workspace.active ? 0.11 : ownsSelection ? 0.10 : 0.045);
        if (drop > 0.001)
            cardSurface = tintedSurface(cardSurface, accent, 0.22 * drop);
        // The narrow Ribbon blades overlap heavily, so blurring each one separately multiplies
        // sampling work without producing a readable difference at that width. The hero remains
        // frosted; blades use the already-tinted opaque surface beneath their live texture.
        drawChromeRect(workspaceBox, cardSurface, damage, round, !ribbonBlade);

        // Hairlines establish the card edge. The focused carousel card uses small corner locks as
        // its only bright signal, echoing Quattro's restrained theme-switcher selection state.
        drawInactiveBorder(workspaceBox, withAlpha(foreground, cardAlpha * (ribbonBlade ? 0.16 : 0.065)), round, 1);
        if (carouselFocused) {
            if (m_chrome.selectedBorder)
                drawSelectedBorder(workspaceBox, withAlpha(accentLit, cardAlpha * 0.76), round, ribbonFocused ? 3 : 2);
            else
                drawBorder(workspaceBox, withAlpha(accentLit, cardAlpha * 0.62), withAlpha(accent, cardAlpha * 0.18),
                    2.62F, static_cast<float>(cardAlpha * 0.76), m_chrome.radius(round), m_chrome.borderWidth(ribbonFocused ? 3 : 2));
        }
        if (ribbonBlade) {
            // A bright leading edge and darker trailing edge give the narrow surface the same
            // directional, skewed read as Omarchy's clipped theme slices without introducing a
            // compositor-side polygon mask for every live preview.
            const auto onLeft = workspaceBox.x < frame.bounds.width / 2.0;
            const auto signalX = onLeft ? workspaceBox.x + workspaceBox.w - 2.0 : workspaceBox.x;
            const auto shadeX  = onLeft ? workspaceBox.x : workspaceBox.x + workspaceBox.w - 2.0;
            drawRect(CBox{signalX, workspaceBox.y + 8.0, 2.0, std::max(1.0, workspaceBox.h - 16.0)},
                withAlpha(accentLit, cardAlpha * 0.32), damage);
            drawRect(CBox{shadeX, workspaceBox.y + 3.0, 2.0, std::max(1.0, workspaceBox.h - 6.0)},
                withAlpha(Theme::shadowColor(), cardAlpha * 0.72), damage);
        }

        // Triangle pulse avoids transcendental work in the per-card render path while keeping the
        // same zero-at-ends, brightest-at-midpoint signal envelope.
        const auto entranceSignal = 1.0 - std::abs(cardEntrance * 2.0 - 1.0);
        if (motion == MotionPreference::Cyberpunk && entranceSignal > 0.001) {
            const auto ghostOffset = workspaceIndex % 2 == 0 ? -10.0 : 10.0;
            const auto ghostBox = CBox{workspaceBox.x + ghostOffset * entranceSignal, workspaceBox.y,
                workspaceBox.w, workspaceBox.h};
            drawSelectedBorder(ghostBox, withAlpha(accentLit, cardAlpha * 0.20 * entranceSignal), round, 1);
        } else if (motion == MotionPreference::Tron && entranceSignal > 0.001) {
            const auto scanX = workspaceBox.x + workspaceBox.w * cardEntrance;
            drawRect(CBox{scanX, workspaceBox.y + 5.0, 2.0, std::max(1.0, workspaceBox.h - 10.0)},
                withAlpha(accentLit, cardAlpha * 0.62 * entranceSignal), damage, 1);
        }
        if (workspace.active && !frame.carousel) {
            const auto railWidth = std::min(48.0, workspaceBox.w * 0.18);
            drawRect(CBox{workspaceBox.x + 16.0, workspaceBox.y, railWidth, 1.0},
                withAlpha(accent, cardAlpha * 0.58), damage, 1);
        }
        if (drop > 0.001) {
            // Destination cue: a halo, a full accent ring and an inset rail, all keyed to the same
            // progress so the card the pointer entered lights up rather than switching on.
            const auto spread = std::lerp(4.0, 14.0, drop);
            if (m_chrome.effects)
                drawChromeRect(CBox{workspaceBox.x - spread, workspaceBox.y - spread, workspaceBox.w + spread * 2.0, workspaceBox.h + spread * 2.0},
                    withAlpha(accent, cardAlpha * 0.12 * drop), damage, round + static_cast<int>(spread));
            drawSelectedBorder(workspaceBox, withAlpha(accentLit, cardAlpha * std::lerp(0.20, 0.86, drop)), round, 2);
            drawSelectedBorder(insetBox(workspaceBox, 7.0), withAlpha(accent, cardAlpha * 0.26 * drop), std::max(1, round - 5), 1, -5);
        }

        const auto headerHeight = frame.ribbon ? 0.0 : compact ? 26.0 : carouselFocused ? std::clamp(workspaceBox.h * 0.105, 40.0, 52.0) :
                                                                  std::clamp(workspaceBox.h * 0.18, 30.0, 38.0);
        if (!frame.ribbon) {
            drawRect(CBox{workspaceBox.x + 14.0, workspaceBox.y + headerHeight, std::max(0.0, workspaceBox.w - 28.0), 1.0},
                withAlpha(carouselFocused || ownsSelection ? accent : foreground,
                    cardAlpha * (carouselFocused ? 0.22 : ownsSelection ? 0.12
                                                                        : 0.075)),
                damage);
        }
        const auto workspaceCode = std::format("{:02}", workspace.workspaceId);
        const auto workspaceLabel = workspace.createTarget ? std::string{"+"} :
            workspace.name.empty() || workspace.name == std::to_string(workspace.workspaceId) ? workspaceCode : workspace.name;
        if (!frame.ribbon) {
            m_labels.renderColored(workspaceLabel, workspaceBox.x + (compact ? 10.0 : 15.0),
                workspaceBox.y + (compact ? 7.0 : 9.0), std::max(1.0, workspaceBox.w - (compact ? 20.0 : 30.0)),
                compact ? Theme::hintSize() : Theme::labelSize(), carouselFocused || ownsSelection ? accentLit : foreground,
                detailAlpha * (carouselFocused ? 1.0 : ownsSelection ? 0.96
                                                                     : 0.82),
                damage);
        }

        if (workspace.empty) {
            const auto markerSize = carouselFocused ? 42.0 : compact ? 20.0 : 30.0;
            const auto centerX = workspaceBox.x + workspaceBox.w / 2.0;
            const auto centerY = workspaceBox.y + headerHeight + (workspaceBox.h - headerHeight) * 0.45;
            drawRect(CBox{centerX - markerSize / 2.0, centerY, markerSize, 1.0},
                withAlpha(workspace.createTarget ? accentLit : foreground, cardAlpha * (workspace.createTarget ? 0.58 : 0.16)), damage, 1);
            drawRect(CBox{centerX, centerY - markerSize / 2.0, 1.0, markerSize},
                withAlpha(workspace.createTarget ? accentLit : foreground, cardAlpha * (workspace.createTarget ? 0.58 : 0.16)), damage, 1);
            if (!compact && workspace.createTarget) {
                m_labels.renderCentered("CREATE",
                    CBox{workspaceBox.x + 10.0, centerY + markerSize / 2.0 + 8.0, std::max(1.0, workspaceBox.w - 20.0), 24.0},
                    Theme::badgeSize(), accentLit, detailAlpha * 0.88, damage);
            }
        }

        for (const auto& window : workspace.windows) {
            if (ribbonBlade) {
                // A 108px picker slice cannot communicate a complete window grid. One clipped live
                // texture gives it the same visual identity at a fraction of the render passes.
                // The expanded hero still renders every window with full selection affordances.
                if (window.stableId != workspace.windows.front().stableId)
                    continue;
                const auto previewShell = insetBox(workspaceBox, 3.0);
                drawChromeRect(previewShell, surfaceColor(0.08F, cardAlpha * 0.90), damage, 1);
                renderWindowPreview(window, previewShell, cardAlpha * 0.82, damage);
                continue;
            }

            const auto windowSelected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
                m_selectedTarget,
                {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
            const auto held    = m_pointerDownTarget.type == OverviewTargetType::Window && window.stableId == m_pointerDownTarget.windowId;
            const auto dragged = held && m_dragging;
            const auto slotRect = remapRect(window.rect, workspace.rect, displayRect);
            auto windowRect = slotRect;
            if (dragged)
                draggedSlot = slotRect;
            if (windowSelected)
                windowRect = scaledAroundCenter(windowRect, std::lerp(1.0, 1.018, selection), -3.0 * selection);
            // The card the pointer is carrying is drawn separately, so what stays in the wall is the
            // hole it left: pressed in slightly, then shrinking and fading as the drag lifts it out.
            if (dragged)
                windowRect = scaledAroundCenter(windowRect, std::lerp(1.0, 0.93, dragLift));
            else if (held && pressDip > 0.0)
                windowRect = scaledAroundCenter(windowRect, std::lerp(1.0, 0.972, pressDip), 1.5 * pressDip);
            const auto windowBox   = boxFor(windowRect);
            const auto windowRound = Theme::windowRadius();
            const auto footerHeight = compact || carouselThumbnail ? 0.0 : 28.0;
            const auto windowAlpha  = dragged ? cardAlpha * std::lerp(1.0, 0.16, dragLift) : cardAlpha;
            const auto windowDetail = dragged ? detailAlpha * std::lerp(1.0, 0.16, dragLift) : detailAlpha;

            if (m_chrome.effects && windowSelected)
                drawChromeRect(CBox{windowBox.x - 6.0, windowBox.y - 6.0, windowBox.w + 12.0, windowBox.h + 12.0},
                    withAlpha(accent, windowAlpha * 0.055 * selection), damage, windowRound + 6);
            if (m_chrome.effects)
                drawChromeRect(CBox{windowBox.x + 3.0, windowBox.y + 5.0 + selection * (windowSelected ? 2.0 : 0.0), windowBox.w, windowBox.h},
                    withAlpha(Theme::shadowColor(), windowAlpha * (windowSelected ? 0.52 : 0.34)), damage, windowRound + 2);

            auto windowSurface = surfaceColor(windowSelected ? 0.25F : 0.19F, windowAlpha * 0.82);
            windowSurface = tintedSurface(windowSurface, accent, windowSelected ? 0.18 * selection : 0.055);
            drawChromeRect(windowBox, windowSurface, damage, windowRound);

            if (carouselThumbnail && (!compact || ribbonBlade) && windowBox.h > 28.0) {
                const auto previewShell = CBox{
                    windowBox.x + 5.0,
                    windowBox.y + 5.0,
                    std::max(1.0, windowBox.w - 10.0),
                    std::max(1.0, windowBox.h - 10.0),
                };
                drawChromeRect(previewShell, surfaceColor(0.08F, windowAlpha * 0.92), damage, std::max(2, windowRound - 2));
                renderWindowPreview(window, previewShell, windowAlpha, damage);
            } else if (!compact && windowBox.h > 88.0) {
                const auto previewShell = CBox{
                    windowBox.x + 7.0,
                    windowBox.y + 7.0,
                    std::max(1.0, windowBox.w - 14.0),
                    std::max(1.0, windowBox.h - footerHeight - 10.0),
                };
                drawChromeRect(previewShell, surfaceColor(0.08F, windowAlpha * 0.92), damage, windowRound - 2);
                renderWindowPreview(window, previewShell, windowAlpha, damage);

                m_labels.renderColored(appGlyph(window.appClass), windowBox.x + 11.0, windowBox.y + windowBox.h - footerHeight + 8.0,
                    18.0, Theme::hintSize(), accent, windowDetail * (windowSelected ? 1.0 : 0.70), damage);
                m_labels.render(window.label, windowBox.x + 32.0, windowBox.y + windowBox.h - footerHeight + 7.0,
                    std::max(1.0, windowBox.w - 43.0), Theme::footerSize(), windowDetail * (windowSelected ? 1.0 : 0.78), damage);
            } else {
                m_labels.render(window.label, windowBox.x + (compact ? 8.0 : 12.0), windowBox.y + (compact ? 5.0 : 8.0),
                    std::max(1.0, windowBox.w - (compact ? 16.0 : 20.0)), compact ? Theme::badgeSize() : Theme::footerSize(),
                    windowDetail * (windowSelected ? 1.0 : 0.76), damage);
            }

            if (dragged) {
                // Outline of the vacated slot, so the wall keeps showing where the window came from
                // and where a cancelled drag will put it back.
                drawSelectedBorder(boxFor(slotRect), withAlpha(accent, cardAlpha * 0.34 * dragLift), windowRound, 1);
            }

            if (windowSelected) {
                drawSelectedBorder(windowBox, withAlpha(accentLit, windowAlpha * std::lerp(0.22, 0.66, selection)),
                    windowRound, 1);
                drawRect(CBox{windowBox.x + 12.0, windowBox.y, std::min(72.0, windowBox.w * 0.34), 1.0},
                    withAlpha(accentLit, windowAlpha * 0.78 * selection), damage, 1);
                drawSignalLock(windowRect, selectionProgress, accentLit, windowAlpha, damage);
            }
        }

        const auto hasCustomWorkspaceName = !workspace.createTarget && !workspace.name.empty() &&
            workspace.name != std::to_string(workspace.workspaceId);
        if (ribbonFocused && hasCustomWorkspaceName) {
            const auto caption = CBox{workspaceBox.x, workspaceBox.y + workspaceBox.h + 14.0,
                workspaceBox.w, 28.0};
            m_labels.renderCentered(workspace.name, caption, Theme::labelSize(), accentLit,
                detailAlpha * 0.92, damage);
        }

        if (workspaceSelected || dropTarget)
            drawSignalLock(displayRect, dropTarget ? dropProgress : selectionProgress, accentLit, cardAlpha, damage);

        // Drawn after the windows so the caption sits over the cards already in the workspace rather
        // than behind them. It rises into place from under the card edge, and is dropped entirely on
        // cards too small to carry it.
        constexpr auto captionWidth  = 118.0;
        constexpr auto captionHeight = 24.0;
        if (drop > 0.001 && workspaceBox.w > captionWidth + 24.0 && workspaceBox.h > 96.0) {
            const auto caption = CBox{
                workspaceBox.x + centered(workspaceBox.w, captionWidth),
                workspaceBox.y + workspaceBox.h - captionHeight - 12.0 + (1.0 - drop) * 10.0,
                captionWidth,
                captionHeight,
            };
            if (m_chrome.effects)
                drawChromeRect(CBox{caption.x + 2.0, caption.y + 3.0, caption.w, caption.h}, withAlpha(Theme::shadowColor(), cardAlpha * 0.50 * drop), damage, 10);
            drawChromeRect(caption, withAlpha(tintedSurface(surfaceColor(0.10F, 1.0), accent, 0.22), cardAlpha * 0.94 * drop), damage, 10);
            drawSelectedBorder(caption, withAlpha(accent, cardAlpha * 0.44 * drop), 10, 1);
            m_labels.renderCentered(workspace.createTarget ? "NEW WORKSPACE" : "MOVE HERE", caption, Theme::badgeSize(),
                accentLit, cardAlpha * drop, damage);
        }
    }

    renderDragOverlay(frame, draggedSlot, contentAlpha, damage);

    if (searchActive) {
        auto dim = m_config.backgroundColor();
        dim.a = static_cast<float>(0.72 * alpha);
        drawRect(CBox{0.0, 0.0, frame.bounds.width, frame.bounds.height}, dim, damage);
        renderSearchPanel(frame, alpha, damage);
    }
}

void OverlayRenderer::renderHintDock(const WorkspaceWallFrame& frame, double contentAlpha, CHyprColor accent, const CRegion& damage) {
    const auto dockProgress = std::clamp(m_dockTransition.value(), 0.0, 1.0);
    if (!m_searchActive && dockProgress > 0.001) {
        const auto dockAlpha = contentAlpha * dockProgress;

        struct DeckHint {
            const char* keys;
            const char* action;
        };
        const auto bindings = KeyboardBindings{
            .vimKeys = m_config.vimKeys(),
            .tabCyclesWindows = m_config.tabCyclesWindows(),
        };
        std::vector<DeckHint> hints{
            {bindings.vimKeys ? "\xe2\x86\x90\xe2\x86\x92/hl" : "\xe2\x86\x90\xe2\x86\x92", "workspace"},
            {bindings.vimKeys ? "\xe2\x86\x91\xe2\x86\x93/kj" : "\xe2\x86\x91\xe2\x86\x93", "window"},
        };
        if (bindings.tabCyclesWindows) {
            hints.push_back({"tab", "next window"});
            if (effectiveLayoutMode() == LayoutMode::Stage)
                hints.push_back({"ctrl+tab", "arrangement"});
        } else if (effectiveLayoutMode() == LayoutMode::Stage) {
            hints.push_back({"tab", "apps/spatial"});
        }
        hints.push_back({"\xe2\x86\xb5", "open"});
        hints.push_back({"/", "find"});
        hints.push_back({"ctrl+,", "settings"});

        constexpr auto dockHeight   = 26.0;
        constexpr auto dockPadX     = 15.0;
        constexpr auto keysGap      = 6.0;
        constexpr auto pairGap      = 15.0;
        constexpr auto measureWidth = 240.0;
        constexpr auto riseDistance = 14.0;
        constexpr auto rimAngle     = 2.62F;

        const auto foreground = m_config.foregroundColor();
        const auto railSurface = surfaceColor(0.10F, 0.72);
        const auto rimLit      = tintedSurface(accent, foreground, 0.42);
        const auto rimShade    = withAlpha(accent, 0.16);

        auto contentWidth = dockPadX;
        std::array<double, 8> keyWidths{};
        std::array<double, 8> actionWidths{};
        for (std::size_t i = 0; i < hints.size(); ++i) {
            keyWidths[i]    = m_labels.measure(hints[i].keys, measureWidth, Theme::hintSize(), foreground).width;
            actionWidths[i] = m_labels.measure(hints[i].action, measureWidth, Theme::hintSize(), foreground).width;
            contentWidth += keyWidths[i] + keysGap + actionWidths[i] + (i + 1 < hints.size() ? pairGap : 0.0);
        }
        contentWidth += dockPadX;

        const auto dockWidth = std::min(std::max(1.0, frame.bounds.width - 64.0), contentWidth);
        const auto dockY = frame.bounds.height - 34.0 - dockHeight + (1.0 - dockProgress) * riseDistance;
        const auto dock  = CBox{centered(frame.bounds.width, dockWidth), dockY, dockWidth, dockHeight};
        const auto radius = static_cast<int>(std::round(dockHeight / 2.0));

        if (m_chrome.effects) {
            drawChromeRect(CBox{dock.x - 3.0, dock.y + 9.0, dock.w + 6.0, dock.h},
                withAlpha(Theme::shadowColor(), dockAlpha * 0.40), damage, radius + 6);
            drawChromeRect(CBox{dock.x + 3.0, dock.y + 5.0, dock.w - 6.0, dock.h},
                withAlpha(Theme::shadowColor(), dockAlpha * 0.55), damage, radius);
        }
        drawChromeRect(dock, withAlpha(railSurface, dockAlpha * 0.90), damage, radius, true);
        if (m_chrome.inactiveBorder)
            drawInactiveBorder(dock, withAlpha(rimLit, dockAlpha * 0.55), radius, 1);
        else
            drawBorder(dock, withAlpha(rimLit, dockAlpha * 0.55), rimShade, rimAngle,
                static_cast<float>(dockAlpha * 0.55), m_chrome.radius(radius), m_chrome.borderWidth(1));

        auto cursorX = dock.x + dockPadX;
        for (std::size_t i = 0; i < hints.size(); ++i) {
            const auto keySize    = m_labels.measure(hints[i].keys, measureWidth, Theme::hintSize(), foreground);
            const auto actionSize = m_labels.measure(hints[i].action, measureWidth, Theme::hintSize(), foreground);
            m_labels.renderColored(hints[i].keys, cursorX, dock.y + centered(dock.h, keySize.height), measureWidth,
                Theme::hintSize(), rimLit, dockAlpha * 0.96, damage);
            cursorX += keyWidths[i] + keysGap;
            m_labels.renderColored(hints[i].action, cursorX, dock.y + centered(dock.h, actionSize.height), measureWidth,
                Theme::hintSize(), foreground, dockAlpha * 0.58, damage);
            cursorX += actionWidths[i] + pairGap;
        }
    }
}

void OverlayRenderer::renderPreferencesPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    if (!m_preferencesVisible || frame.monitorId != m_preferencesMonitorId)
        return;

    const auto geometry   = computePreferencesPanel(
        frame.bounds, effectiveLayoutMode() == LayoutMode::Stage, nativeThemeOptionCount());
    const auto accent     = resolvedAccentColor();
    const auto foreground = m_config.foregroundColor();
    const auto panelBox   = boxFor(geometry.panel);
    const auto panelAlpha = std::clamp(alpha, 0.0, 1.0);

    auto dim = m_config.backgroundColor();
    dim.a = static_cast<float>(0.58 * panelAlpha);
    drawRect(CBox{0.0, 0.0, frame.bounds.width, frame.bounds.height}, dim, damage);
    constexpr auto panelRadius = 0;
    const auto     panelSurface = surfaceColor(0.0F, panelAlpha * 0.985);
    drawRect(panelBox, panelSurface, damage, panelRadius, m_chrome.effects);
    drawBorder(panelBox, withAlpha(foreground, panelAlpha * 0.24), panelRadius, 1);
    constexpr auto signalLength = 64.0;
    drawRect(CBox{panelBox.x, panelBox.y, signalLength, 2.0}, withAlpha(accent, panelAlpha * 0.94), damage);
    drawRect(CBox{panelBox.x, panelBox.y, 2.0, 28.0}, withAlpha(accent, panelAlpha * 0.94), damage);
    drawRect(CBox{panelBox.x + panelBox.w - signalLength, panelBox.y + panelBox.h - 2.0, signalLength, 2.0},
        withAlpha(accent, panelAlpha * 0.64), damage);
    drawRect(CBox{panelBox.x + panelBox.w - 2.0, panelBox.y + panelBox.h - 28.0, 2.0, 28.0},
        withAlpha(accent, panelAlpha * 0.64), damage);

    const auto closeBox = boxFor(geometry.closeButton);
    const auto closeSelected = m_selectedPreference == PreferenceControl::Close;
    drawRect(closeBox, withAlpha(foreground, panelAlpha * (closeSelected ? 0.08 : 0.0)), damage, 0);
    drawBorder(closeBox, withAlpha(foreground, panelAlpha * (closeSelected ? 0.25 : 0.40)), 0, 1);
    m_labels.renderCentered("X", closeBox, Theme::hintSize(),
        closeSelected ? accent : foreground, panelAlpha * (closeSelected ? 1.0 : 0.72), damage);

    const auto rowLabel = [](PreferenceControl control) -> std::string {
        switch (control) {
        case PreferenceControl::WorkspaceView:
            return "WORKSPACE";
        case PreferenceControl::WindowView:
            return "WINDOWS";
        case PreferenceControl::Shelf:
            return "SHELF";
        case PreferenceControl::Motion:
            return "MOTION";
        case PreferenceControl::Chrome:
            return "CHROME";
        case PreferenceControl::NativeTheme:
            return "THEME";
        case PreferenceControl::None:
        case PreferenceControl::AppExpose:
        case PreferenceControl::Close:
            return {};
        }
        return {};
    };

    for (const auto& row : geometry.rows) {
        const auto selected = row.control == m_selectedPreference;
        if (selected) {
            const auto rowBox = boxFor(row.rect);
            drawRect(rowBox, withAlpha(foreground, panelAlpha * 0.08), damage, 0);
            drawBorder(rowBox, withAlpha(foreground, panelAlpha * 0.25), 0, 1);
        }
        m_labels.renderColored(rowLabel(row.control), row.rect.x + 16.0, row.rect.y + centered(row.rect.height, 12.0),
            126.0, Theme::hintSize(), selected ? accent : foreground,
            panelAlpha * (selected ? 1.0 : 0.58), damage);
    }

    const auto activeOption = [this](PreferenceControl control) {
        switch (control) {
        case PreferenceControl::WorkspaceView:
            if (effectiveLayoutMode() == LayoutMode::WorkspaceWall)
                return 1;
            if (effectiveLayoutMode() == LayoutMode::Carousel)
                return 2;
            if (effectiveLayoutMode() == LayoutMode::Ribbon)
                return 3;
            return 0;
        case PreferenceControl::WindowView:
            return static_cast<int>(m_preferences.state().windowView);
        case PreferenceControl::Shelf:
            return static_cast<int>(m_preferences.state().shelf);
        case PreferenceControl::Motion:
            return static_cast<int>(m_preferences.state().motion);
        case PreferenceControl::Chrome:
            return static_cast<int>(m_preferences.state().chrome);
        case PreferenceControl::NativeTheme:
            return 1;
        case PreferenceControl::None:
        case PreferenceControl::AppExpose:
        case PreferenceControl::Close:
            return -1;
        }
        return -1;
    };
    const auto optionLabel = [this](PreferenceControl control, int value) -> std::string {
        switch (control) {
        case PreferenceControl::WorkspaceView:
            return value == 0 ? "STAGE" : value == 1 ? "WALL"
                                      : value == 2   ? "CAROUSEL"
                                                     : "RIBBON";
        case PreferenceControl::WindowView:
            return value == 0 ? "SPATIAL" : value == 1 ? "GROUPED"
                                                       : "DECK";
        case PreferenceControl::Shelf: {
            static constexpr std::array labels{"CONFIG", "AUTO", "ALWAYS", "HIDDEN"};
            return labels[static_cast<std::size_t>(std::clamp(value, 0, 3))];
        }
        case PreferenceControl::Motion: {
            static constexpr std::array labels{
                "DEFAULT", "SNAP", "GLITCH", "LIGHT", "SILK", "REDUCED", "OFF"};
            return labels[static_cast<std::size_t>(std::clamp(value, 0, 6))];
        }
        case PreferenceControl::Chrome: {
            static constexpr std::array labels{"CONFIG", "RADIANT", "NATIVE", "FLAT"};
            return labels[static_cast<std::size_t>(std::clamp(value, 0, 3))];
        }
        case PreferenceControl::NativeTheme:
            if (value == 0)
                return "<";
            if (value == 2)
                return ">";
            if (const auto selected = selectedNativeThemeIndex(); selected > 0)
                return m_installedThemes[static_cast<std::size_t>(selected - 1)].name;
            return "CURRENT";
        case PreferenceControl::None:
        case PreferenceControl::AppExpose:
        case PreferenceControl::Close:
            return {};
        }
        return {};
    };
    for (const auto& option : geometry.options) {
        const auto optionBox = boxFor(option.rect);
        const auto active    = option.value == activeOption(option.control);
        const auto focused   = active && option.control == m_selectedPreference;
        const auto nativeTheme = option.control == PreferenceControl::NativeTheme;
        if (nativeTheme) {
            drawRect(optionBox, withAlpha(foreground,
                panelAlpha * (option.value == 1 ? focused ? 0.15 : 0.08 : 0.0)), damage, 0);
            drawBorder(optionBox, withAlpha(foreground,
                panelAlpha * (focused ? 0.30 : option.value == 1 ? 0.20 : 0.36)), 0, 1);
            if (option.value != 1) {
                m_labels.renderCentered(optionLabel(option.control, option.value), optionBox,
                    Theme::hintSize(), foreground, panelAlpha * 0.76, damage);
                continue;
            }

            constexpr auto swatchWidth = 14.0;
            constexpr auto swatchGap   = 4.0;
            constexpr auto swatchCount = 3.0;
            const auto swatchesWidth = swatchWidth * swatchCount + swatchGap * (swatchCount - 1.0);
            const auto swatchesX = optionBox.x + optionBox.w - swatchesWidth - 10.0;
            const auto swatches = themePreviewColors(m_config.palette());
            for (std::size_t index = 0; index < swatches.size(); ++index) {
                const auto& swatch = swatches[index];
                drawRect(CBox{swatchesX + static_cast<double>(index) * (swatchWidth + swatchGap),
                             optionBox.y + centered(optionBox.h, 8.0), swatchWidth, 8.0},
                    withAlpha(CHyprColor{swatch.red, swatch.green, swatch.blue, swatch.alpha}, panelAlpha * 0.94), damage, 1);
            }
            m_labels.renderColored(optionLabel(option.control, option.value), optionBox.x + 12.0,
                optionBox.y + centered(optionBox.h, 12.0),
                std::max(1.0, swatchesX - optionBox.x - 20.0), Theme::hintSize(),
                focused ? accent : foreground, panelAlpha * (focused ? 1.0 : 0.78), damage);
            continue;
        }
        drawRect(optionBox, withAlpha(foreground,
            panelAlpha * (active ? 0.18 : 0.0)), damage, 0);
        drawBorder(optionBox, withAlpha(foreground,
            panelAlpha * (focused ? 0.30 : 0.40)), 0, 1);
        m_labels.renderCentered(optionLabel(option.control, option.value), optionBox, Theme::hintSize(),
            active ? accent : foreground, panelAlpha * (active ? 1.0 : 0.72), damage);
    }

    const auto appSelected = m_selectedPreference == PreferenceControl::AppExpose;
    const auto appBox      = boxFor(geometry.appExposeButton);
    drawRect(appBox, withAlpha(foreground, panelAlpha * (appSelected ? 0.08 : 0.0)), damage, 0);
    drawBorder(appBox, withAlpha(foreground, panelAlpha * (appSelected ? 0.25 : 0.40)), 0, 1);
    m_labels.renderCentered("APP WINDOWS", appBox, Theme::hintSize(),
        appSelected ? accent : foreground, panelAlpha * (appSelected ? 1.0 : 0.72), damage);
}

void OverlayRenderer::renderStageWindows(const WorkspaceWallFrame& frame, const StageContext& ctx, const CRegion& damage) {
    const auto dragLift = m_dragging ? easedProgress(m_dragLiftTransition.value()) : 0.0;
    const auto pressDip = m_pointerDown && !m_dragging && m_pointerDownTarget.type == OverviewTargetType::Window
        ? easedProgress(m_pressTransition.value())
                          : 0.0;
    const auto motionSpec = carouselMotion(ctx.motion);
    const auto unresolvedEntrance = 1.0 - ctx.entranceTransition;
    std::optional<LayoutRect> draggedSlot;

    for (const auto& window : frame.stage.windows) {
        // A window that has already unmapped has nothing left to preview, and drawing its card
        // anyway left an empty surface sitting on the stage until the next collect landed.
        if (!findLiveWindow(window.stableId))
            continue;
        const auto selected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
            m_selectedTarget, {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
        const auto closeTransition = window.stableId == m_closingWindowId ? std::clamp(m_windowCloseTransition.value(), 0.0, 1.0) : 1.0;
        const auto cardAlpha       = ctx.stageAlpha * closeTransition;
        const auto held    = m_pointerDownTarget.type == OverviewTargetType::Window && window.stableId == m_pointerDownTarget.windowId;
        const auto dragged = held && m_dragging;
        auto displayRect = remapStageRect(window.rect, frame.stage.bounds, ctx.pushedStageBounds);
        if (motionSpec.stageAlternateHorizontal) {
            const auto direction = window.stableId % 2 == 0 ? -1.0 : 1.0;
            displayRect.x += unresolvedEntrance * motionSpec.stageHorizontalTravel * direction;
        }
        if (dragged)
            draggedSlot = displayRect;
        if (selected)
            displayRect = scaledAroundCenter(displayRect, std::lerp(0.995, 1.014, ctx.selectionTransition), -3.0 * ctx.selectionTransition);
        // What stays on the stage is the hole the card left, not the card: it presses in under the
        // pointer and then shrinks away as the drag lifts the real card out of it.
        if (dragged)
            displayRect = scaledAroundCenter(displayRect, std::lerp(1.0, 0.93, dragLift));
        else if (held && pressDip > 0.0)
            displayRect = scaledAroundCenter(displayRect, std::lerp(1.0, 0.975, pressDip), 1.5 * pressDip);
        if (closeTransition < 1.0)
            displayRect = windowDismissalRect(displayRect, closeTransition);
        const auto windowBox = boxFor(displayRect);
        const auto radius = Theme::windowRadius();

        if (window.appGroupStart && m_mode != OverviewMode::Grouped) {
            m_labels.renderColored(appGlyph(window.appClass), displayRect.x + 2.0, displayRect.y - 22.0,
                18.0, Theme::hintSize(), ctx.accent, cardAlpha * 0.92, damage);
            m_labels.render(window.appClass, displayRect.x + 24.0, displayRect.y - 22.0,
                std::max(1.0, displayRect.width - 26.0), Theme::hintSize(), cardAlpha * 0.68, damage);
        }

        const auto windowAlpha = dragged ? cardAlpha * std::lerp(1.0, 0.18, dragLift) : cardAlpha;
        const auto lift = selected ? ctx.selectionTransition : 0.0;
        // Two shadow layers: a wide ambient one plus a tighter contact shadow, so cards float
        // instead of sitting flat on the backdrop.
        if (m_chrome.effects) {
            drawChromeRect(CBox{windowBox.x - 4.0, windowBox.y + 6.0 + lift * 8.0, windowBox.w + 8.0, windowBox.h + 8.0},
                withAlpha(Theme::shadowColor(), windowAlpha * (0.34 + lift * 0.24)), damage, radius + 10);
            drawChromeRect(CBox{windowBox.x + 5.0, windowBox.y + 9.0 + lift * 5.0, windowBox.w, windowBox.h},
                withAlpha(Theme::shadowColor(), windowAlpha * (0.62 + lift * 0.20)), damage, radius + 2);
        }
        if (m_chrome.effects && selected) {
            drawChromeRect(CBox{windowBox.x - 8.0, windowBox.y - 8.0, windowBox.w + 16.0, windowBox.h + 16.0},
                withAlpha(ctx.accent, cardAlpha * 0.10 * ctx.selectionTransition), damage, radius + 8);
        }
        drawChromeRect(windowBox, withAlpha(ctx.stageSurface, windowAlpha), damage, radius);
        renderWindowPreview(window, windowBox, windowAlpha, damage);
        // Glass top edge: a hairline highlight along the upper border, the glass card cue that
        // separates a floating surface from a flat rectangle.
        drawRect(CBox{windowBox.x + radius * 0.6, windowBox.y, std::max(0.0, windowBox.w - radius * 1.2), 1.0},
            surfaceColor(0.60F, windowAlpha * 0.20), damage);

        if (selected) {
            drawSelectedBorder(CBox{windowBox.x - 1.0, windowBox.y - 1.0, windowBox.w + 2.0, windowBox.h + 2.0}, withAlpha(ctx.accent, cardAlpha * 0.82),
                radius + 1, 1);
            drawSignalLock(displayRect, ctx.selectionTransition, ctx.accent, windowAlpha, damage);
        } else if (m_chrome.inactiveBorder)
            drawInactiveBorder(windowBox, withAlpha(ctx.accent, cardAlpha), radius, 1);

        // Drawn after the preview so it sits over the thumbnail rather than under it.
        const auto closeProgress = window.stableId == m_closeButtonWindowId ? std::clamp(m_closeButtonTransition.value(), 0.0, 1.0) : 0.0;
        if (closeProgress > 0.001) {
            // Built from the layout-space card and mapped through the same stage remap the hit test
            // inverts, so the drawn button and its hotspot are the same rectangle. Deriving it from
            // displayRect instead put it inside the selection scale, which only applies while the
            // card is hovered: the button drifted off its hotspot exactly when it was being used,
            // and along the edge that turned into a hover/unhover loop.
            const auto closeRect = closeButtonRect(window.rect);
            if (closeRect.width > 0.0) {
                constexpr auto CLOSE_GLYPH = "\xe2\x9c\x95";
                // Measured against a box far wider than the button. Handing the 22px button width
                // to the text layout made the glyph lay out inside a 22px line, and centring it
                // against that constrained box left it sitting off to one side.
                constexpr auto GLYPH_MEASURE_WIDTH = 64.0;

                const auto hotProgress = std::clamp(m_closeButtonHotTransition.value(), 0.0, 1.0);
                // Grows into place on reveal but never past its hotspot. Swelling under the pointer
                // put the button's edge outside the area that reports it hot, so resting there
                // toggled hot off and on and the button flickered.
                const auto scaled   = scaledAroundCenter(closeRect, std::lerp(0.82, 1.0, closeProgress), 0.0);
                const auto closeBox = boxFor(remapStageRect(scaled, frame.stage.bounds, ctx.pushedStageBounds));
                const auto dot      = static_cast<int>(std::round(closeBox.w / 2.0));
                const auto reveal   = cardAlpha * closeProgress;

                // Hot reads as a halo outside the button instead of extra size: decoration can
                // safely overhang the hotspot, geometry cannot.
                if (m_chrome.effects && hotProgress > 0.001)
                    drawChromeRect(CBox{closeBox.x - 5.0, closeBox.y - 5.0, closeBox.w + 10.0, closeBox.h + 10.0},
                        withAlpha(ctx.accent, reveal * 0.24 * hotProgress), damage, dot + 5);
                if (m_chrome.effects)
                    drawChromeRect(CBox{closeBox.x + 1.0, closeBox.y + 2.0, closeBox.w, closeBox.h},
                        withAlpha(Theme::shadowColor(), reveal * 0.42), damage, dot);
                // Rests as a neutral surface and crossfades into the ctx.accent under the pointer.
                const auto fill = tintedSurface(surfaceColor(0.34F, 1.0), ctx.accent, hotProgress * 0.94);
                // Blur is rectangular before Hyprland applies the corner mask, which leaves a
                // grey square around this fully round control on hover.
                drawChromeRect(closeBox, withAlpha(fill, reveal * 0.94), damage, dot);

                const auto glyphColor = tintedSurface(m_config.foregroundColor(), m_config.backgroundColor(), hotProgress);
                const auto glyphSize  = m_labels.measure(CLOSE_GLYPH, GLYPH_MEASURE_WIDTH, Theme::hintSize(), glyphColor);
                m_labels.renderColored(CLOSE_GLYPH, closeBox.x + centered(closeBox.w, glyphSize.width),
                    closeBox.y + centered(closeBox.h, glyphSize.height), GLYPH_MEASURE_WIDTH, Theme::hintSize(), glyphColor,
                    reveal * 0.96, damage);
            }
        }

        const auto titleUpper = std::max(1.0, std::min(380.0, ctx.displayedStageBounds.width - 24.0));
        const auto titleLower = std::min(136.0, titleUpper);
        const auto titleWidth = std::clamp(104.0 + static_cast<double>(window.label.size()) * 6.2, titleLower, titleUpper);
        const auto stageRight = ctx.displayedStageBounds.x + ctx.displayedStageBounds.width;
        const auto titleX = std::clamp(windowBox.x + centered(windowBox.w, titleWidth), ctx.displayedStageBounds.x, std::max(ctx.displayedStageBounds.x, stageRight - titleWidth));
        const auto titleY = std::min(windowBox.y + windowBox.h + 8.0, ctx.displayedStageBounds.y + ctx.displayedStageBounds.height - 26.0);
        const auto titleBox = CBox{titleX, titleY, titleWidth, 26.0};
        auto titleSurface = tintedSurface(ctx.railSurface, ctx.accent, selected ? 0.18 : 0.04);
        // Follows the card's own alpha, so a lifted window does not leave a fully lit title sitting
        // under the hole it came out of.
        if (m_chrome.effects)
            drawChromeRect(CBox{titleBox.x + 3.0, titleBox.y + 4.0, titleBox.w, titleBox.h}, withAlpha(Theme::shadowColor(), windowAlpha * 0.52), damage, 9);
        drawChromeRect(titleBox, withAlpha(titleSurface, windowAlpha * (selected ? 0.96 : 0.78)), damage, 9, true);
        if (selected)
            drawRect(CBox{titleBox.x + 12.0, titleBox.y + titleBox.h - 1.0, std::max(1.0, titleBox.w - 24.0), 1.0}, withAlpha(ctx.accent, windowAlpha * 0.64), damage, 1);
        m_labels.renderColored(appGlyph(window.appClass), titleBox.x + 11.0, titleBox.y + 6.0,
            18.0, Theme::hintSize(), ctx.accent, windowAlpha * (selected ? 1.0 : 0.82), damage);
        m_labels.render(window.label, titleBox.x + 33.0, titleBox.y + 6.0,
            std::max(1.0, titleBox.w - 45.0), Theme::hintSize(), windowAlpha * (selected ? 1.0 : 0.76), damage);
    }

    renderDragOverlay(frame, draggedSlot, ctx.contentAlpha, damage);
}

void OverlayRenderer::renderStageFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {

    const auto searchMultiplier = m_searchActive ? 0.16 : 1.0;
    // Monitors are not dimmed by which one holds the selection: every monitor shows its own
    // workspaces, so re-shading them on a pointer move reads as an unrelated screen flickering.
    const auto contentAlpha = alpha * searchMultiplier;
    const auto accent       = resolvedAccentColor();
    const auto railSurface  = surfaceColor(0.10F, 0.72);
    const auto stageSurface = surfaceColor(0.12F, 1.0);
    const auto motion       = m_preferences.state().motion;
    const auto motionSpec   = carouselMotion(motion);
    auto railBox            = boxFor(frame.rail.bounds);
    // A hover only changes the previewed workspace on the monitor under the pointer. Scoping the
    // stage transition to that monitor stops the others from replaying their entrance animation.
    const auto transition = m_stageTransitionMonitorId == -1 || m_stageTransitionMonitorId == frame.monitorId
        ? std::clamp(m_stageTransition.value(), 0.0, 1.0)
                            : 1.0;
    const auto shelfProgress = std::clamp(m_shelfTransition.value(), 0.0, 1.0);
    const auto displayedStageBounds = interpolatedRect(collapsedStageBounds(frame), frame.stage.bounds, shelfProgress);
    const auto selectionTransition = std::clamp(m_selectionTransition.value(), 0.0, 1.0);
    const auto dropProgress = std::clamp(m_dropTargetTransition.value(), 0.0, 1.0);
    const auto dropGlow = easedProgress(dropProgress);
    const auto railAlpha = contentAlpha * std::clamp(shelfProgress * 1.8, 0.0, 1.0);
    const auto railEntranceOffset = -(1.0 - alpha) * 18.0 + stageRailEntranceOffset(frame, shelfProgress);
    railBox.y += railEntranceOffset;
    const auto previousFrameIt = std::ranges::find_if(m_previousFrames, [&frame](const WorkspaceWallFrame& candidate) {
        return candidate.monitorId == frame.monitorId;
    });
    const auto* previousFrame = previousFrameIt == m_previousFrames.end() ? nullptr : &*previousFrameIt;

    auto gridColor = accent;
    gridColor.a *= static_cast<float>(contentAlpha * 0.018);
    for (double x = 0.0; x < frame.bounds.width; x += 192.0)
        drawRect(CBox{x, displayedStageBounds.y - 24.0, 1.0, displayedStageBounds.height + 30.0}, gridColor, damage);
    for (double y = displayedStageBounds.y; y < displayedStageBounds.y + displayedStageBounds.height; y += 120.0)
        drawRect(CBox{0.0, y, frame.bounds.width, 1.0}, gridColor, damage);

    for (const auto& workspace : frame.workspaces) {
        auto displayRect = workspace.rect;
        displayRect.y += railEntranceOffset;
        if (previousFrame && transition < 1.0) {
            const auto previousWorkspace = std::ranges::find_if(previousFrame->workspaces, [&workspace](const WorkspaceCard& candidate) {
                return candidate.workspaceId == workspace.workspaceId;
            });
            if (previousWorkspace != previousFrame->workspaces.end())
                displayRect.x = std::lerp(previousWorkspace->rect.x, workspace.rect.x, transition);
        }

        auto visibleRailBounds = frame.rail.bounds;
        visibleRailBounds.y += railEntranceOffset;
        if (!intersects(displayRect, visibleRailBounds))
            continue;

        const auto selected = workspace.workspaceId == m_selectedTarget.workspaceId && frame.monitorId == m_selectedFrameMonitorId;
        const auto workspaceSelected = selected &&
            (m_selectedTarget.type == OverviewTargetType::Workspace || m_selectedTarget.type == OverviewTargetType::NewWorkspace);
        const auto dropTarget = m_dragging && m_dragTarget.type != OverviewTargetType::None &&
            m_dragTarget.monitorId == frame.monitorId && m_dragTarget.workspaceId == workspace.workspaceId;
        const auto drop = dropTarget ? dropGlow : 0.0;
        // Selection lifts the card, brightens it, and wraps it in an accent ring with a soft glow.
        if (selected)
            displayRect = scaledAroundCenter(displayRect, std::lerp(0.995, 1.032, selectionTransition), -5.0 * selectionTransition);
        if (dropTarget)
            displayRect = scaledAroundCenter(displayRect, std::lerp(1.0, 1.04, drop), -5.0 * drop);
        const auto cardBox  = boxFor(displayRect);
        const auto radius   = Theme::workspaceRadius(true);

        if (m_chrome.effects && selected && !workspace.createTarget) {
            drawChromeRect(CBox{cardBox.x - 16.0, cardBox.y - 16.0, cardBox.w + 32.0, cardBox.h + 32.0},
                withAlpha(accent, railAlpha * (0.07 * selectionTransition + 0.10 * drop)), damage, radius + 16);
            drawChromeRect(CBox{cardBox.x - 8.0, cardBox.y - 8.0, cardBox.w + 16.0, cardBox.h + 16.0},
                withAlpha(accent, railAlpha * (0.15 * selectionTransition + 0.18 * drop)), damage, radius + 8);
        }

        const auto lift = selected ? selectionTransition : 0.0;

        if (!workspace.createTarget) {
            if (m_chrome.effects)
                drawChromeRect(CBox{cardBox.x + 3.0, cardBox.y + 6.0 + lift * 5.0, cardBox.w, cardBox.h},
                    withAlpha(Theme::shadowColor(), railAlpha * (0.44 + lift * 0.36)), damage, radius);
            // Selection carries roughly twice the lift of an idle card so the highlight is
            // legible at a glance rather than a few percent apart.
            const auto cardLift = workspace.empty ? 0.07F : selected ? 0.26F : 0.13F;
            const auto cardOpacity = workspace.empty ? 0.52 : selected ? 0.97 : 0.82;
            auto cardFill = surfaceColor(cardLift, railAlpha * cardOpacity);
            if (selected)
                cardFill = tintedSurface(cardFill, accent, 0.12);
            if (dropTarget)
                cardFill = tintedSurface(cardFill, accent, 0.24 * drop);
            drawChromeRect(cardBox, cardFill, damage, radius);
        }

        if (workspace.createTarget) {
            // A full-size outlined card rather than a small floating circle, so the create target
            // sits in the workspace row instead of orbiting beside it.
            if (m_chrome.effects)
                drawChromeRect(CBox{cardBox.x + 3.0, cardBox.y + 6.0 + lift * 5.0, cardBox.w, cardBox.h},
                    withAlpha(Theme::shadowColor(), railAlpha * (0.22 + lift * 0.30)), damage, radius);
            // Needs a real surface, not just an accent wash: a translucent tint let the desktop
            // read straight through the card and made it look like a rendering artefact.
            auto createFill = surfaceColor(selected ? 0.20F : 0.11F, railAlpha * (selected ? 0.93 : 0.76));
            createFill = tintedSurface(createFill, accent, selected ? 0.18 : 0.10);
            drawChromeRect(cardBox, createFill, damage, radius, true);
            const auto ringStrength = selected ? 0.88 : 0.46;
            if (selected)
                drawSelectedBorder(cardBox, withAlpha(accent, railAlpha * ringStrength), radius, 2);
            else
                drawInactiveBorder(cardBox, withAlpha(accent, railAlpha * ringStrength), radius, 1);
            const auto glyphBox = CBox{cardBox.x, cardBox.y + centered(cardBox.h, 36.0) - 7.0, cardBox.w, 36.0};
            m_labels.renderCentered("+", glyphBox, Theme::titleSize() + 14, accent, railAlpha * (selected ? 1.0 : 0.78), damage);
            const auto captionBox = CBox{cardBox.x, cardBox.y + cardBox.h - 27.0, cardBox.w, 16.0};
            m_labels.renderCentered("New", captionBox, Theme::badgeSize(), m_config.foregroundColor(),
                railAlpha * (selected ? 0.82 : 0.56), damage);
        }

        for (const auto& window : workspace.windows) {
            const auto previewBox = boxFor(remapRect(window.rect, workspace.rect, displayRect));
            renderWindowPreview(window, previewBox, railAlpha, damage);
        }

        if (workspaceSelected || dropTarget)
            drawSignalLock(displayRect, dropTarget ? dropProgress : selectionTransition, accent, railAlpha, damage);

        if (!workspace.createTarget) {
            const auto borderStrength = dropTarget ? std::lerp(0.50, 1.0, drop) : selected ? 0.95 : workspace.active ? 0.30 : 0.10;
            if (selected || dropTarget)
                drawSelectedBorder(cardBox, withAlpha(accent, railAlpha * borderStrength), radius, 2);
            else
                drawInactiveBorder(cardBox, withAlpha(accent, railAlpha * borderStrength), radius, 1);
            if (dropTarget)
                drawSelectedBorder(insetBox(cardBox, 6.0), withAlpha(accent, railAlpha * 0.32 * drop), std::max(1, radius - 4), 1, -4);
        }

        if (workspace.active && !selected && !workspace.createTarget) {
            const auto lineWidth = std::min(18.0, std::max(8.0, cardBox.w - 32.0));
            drawRect(CBox{cardBox.x + centered(cardBox.w, lineWidth), cardBox.y + cardBox.h - 4.0, lineWidth, 2.0},
                withAlpha(accent, railAlpha * 0.56), damage, 1);
        }
    }

    if (frame.rail.overflowLeft)
        m_labels.render("\xe2\x80\xb9", frame.rail.bounds.x + 5.0, frame.rail.bounds.y + railEntranceOffset + frame.rail.bounds.height / 2.0 - 10.0,
            20.0, Theme::titleSize(), railAlpha * 0.72, damage);
    if (frame.rail.overflowRight)
        m_labels.render("\xe2\x80\xba", frame.rail.bounds.x + frame.rail.bounds.width - 20.0,
            frame.rail.bounds.y + railEntranceOffset + frame.rail.bounds.height / 2.0 - 10.0, 16.0, Theme::titleSize(), railAlpha * 0.72, damage);

    // The incoming stage follows the same selected motion language as Wall and Carousel.
    const auto stageAlpha        = contentAlpha * transition;
    auto pushedStageBounds = scaledAroundCenter(displayedStageBounds, std::lerp(motionSpec.stageStartScale, 1.0, transition));
    const auto unresolvedStage = 1.0 - transition;
    pushedStageBounds.x += unresolvedStage * (motionSpec.stageAlternateHorizontal ? 0.0 : motionSpec.stageHorizontalTravel);
    pushedStageBounds.y += unresolvedStage * motionSpec.stageVerticalTravel;

    const auto entranceSignal = 1.0 - std::abs(transition * 2.0 - 1.0);
    if (motion == MotionPreference::Tron && entranceSignal > 0.001) {
        const auto scanX = pushedStageBounds.x + pushedStageBounds.width * transition;
        drawRect(CBox{scanX, pushedStageBounds.y, 2.0, pushedStageBounds.height},
            withAlpha(accent, stageAlpha * 0.38 * entranceSignal), damage, 1);
    }

    if (previousFrame && previousFrame->stage.workspaceId != frame.stage.workspaceId && transition < 1.0) {
        const auto previousAlpha = contentAlpha * (1.0 - transition);
        const auto previousScale = std::lerp(1.0, 0.92, transition);
        for (const auto& window : previousFrame->stage.windows) {
            const auto previousDisplayedStage = scaledAroundCenter(
                interpolatedRect(collapsedStageBounds(*previousFrame), previousFrame->stage.bounds, shelfProgress), previousScale);
            const auto previousBox = boxFor(remapStageRect(window.rect, previousFrame->stage.bounds, previousDisplayedStage));
            const auto radius = Theme::windowRadius();
            if (m_chrome.effects)
                drawChromeRect(CBox{previousBox.x + 7.0, previousBox.y + 10.0, previousBox.w, previousBox.h},
                    withAlpha(Theme::shadowColor(), previousAlpha * 0.72), damage, radius + 2);
            drawChromeRect(previousBox, surfaceColor(0.12F, previousAlpha), damage, radius);
            renderWindowPreview(window, previousBox, previousAlpha, damage);
        }
    }
    if (frame.stage.empty) {
        m_labels.render("Empty workspace", pushedStageBounds.x + centered(pushedStageBounds.width, 180.0),
            pushedStageBounds.y + centered(pushedStageBounds.height, 24.0), 180.0, Theme::footerSize(), stageAlpha * 0.42, damage);
    }

    const StageContext stageCtx{
        .contentAlpha = contentAlpha,
        .stageAlpha           = stageAlpha,
        .entranceTransition   = transition,
        .selectionTransition  = selectionTransition,
        .motion               = motion,
        .accent               = accent,
        .stageSurface         = stageSurface,
        .railSurface          = railSurface,
        .displayedStageBounds = displayedStageBounds,
        .pushedStageBounds    = pushedStageBounds,
    };
    renderStageWindows(frame, stageCtx, damage);

    if (m_searchActive) {
        auto dim = m_config.backgroundColor();
        dim.a = static_cast<float>(0.58 * alpha);
        drawRect(CBox{0.0, 0.0, frame.bounds.width, frame.bounds.height}, dim, damage);
        renderSearchPanel(frame, alpha, damage);
    }
}

void OverlayRenderer::renderWindowPreview(const WindowCard& windowCard, const CBox& clipBox, double alpha, const CRegion& damage) {
    if (!g_pHyprRenderer || alpha <= 0.001 || clipBox.w <= 0.0 || clipBox.h <= 0.0)
        return;

    const auto window = findLiveWindow(windowCard.stableId);
    if (!window)
        return;

    const auto wlSurface = window->wlSurface();
    if (!wlSurface || !wlSurface->exists())
        return;

    const auto surface = wlSurface->resource();
    if (!surface || !surface->good())
        return;

    const auto texture = currentSurfaceTexture(surface);
    if (!texture)
        return;

    const auto sourceSize = texture->m_size == Vector2D{} ? HyprlandCompat::windowSize(window) : texture->m_size;
    const auto targetBox  = fillBoxForAspect(insetBox(clipBox, 2.0), sourceSize.x, sourceSize.y);
    if (targetBox.w <= 0.0 || targetBox.h <= 0.0)
        return;

    CTexPassElement::SRenderData data;
    data.tex      = texture;
    data.box      = targetBox;
    data.a        = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    data.overallA = data.a;
    data.damage   = damage;
    data.round    = m_chrome.radius(Theme::windowRadius());
    data.clipBox  = clipBox;
    data.surface  = surface;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
}

void OverlayRenderer::renderDragOverlay(const WorkspaceWallFrame& frame, std::optional<LayoutRect> draggedSlot, double alpha, const CRegion& damage) {
    if (m_dragging) {
        const auto  bounds = m_frameBoundsByMonitor.find(frame.monitorId);
        const auto* source = findWindowCard(m_pointerDownTarget.windowId);
        if (bounds == m_frameBoundsByMonitor.end() || !source || !contains(bounds->second, m_pointerPosition.x, m_pointerPosition.y))
            return;

        const auto local = mapGlobalPointToFrame(bounds->second, frame.bounds, m_pointerPosition.x, m_pointerPosition.y);
        const auto lift  = easedProgress(m_dragLiftTransition.value());
        renderDragCard(*source, dragCardRect(draggedSlot.value_or(source->rect), local, lift), alpha, lift, damage);
        return;
    }

    if (!m_dragSettleTransition.renderable() || m_dragSettle.monitorId != frame.monitorId)
        return;

    const auto* source = findWindowCard(m_dragSettle.windowId);
    if (!source)
        return;

    // The settle runs 1 → 0 after release, so the card covers the last stretch into the workspace it
    // was dropped on — or back into its own slot — instead of vanishing wherever the pointer stopped.
    const auto remaining = std::clamp(m_dragSettleTransition.value(), 0.0, 1.0);
    renderDragCard(*source, interpolatedRect(m_dragSettle.from, m_dragSettle.to, easedProgress(1.0 - remaining)),
        alpha * remaining, remaining, damage);
}

void OverlayRenderer::renderDragCard(const WindowCard& window, const LayoutRect& rect, double alpha, double lift, const CRegion& damage) {
    if (alpha <= 0.001 || rect.width <= 0.0 || rect.height <= 0.0)
        return;

    const auto box    = boxFor(rect);
    const auto radius = Theme::windowRadius();
    const auto accent = resolvedAccentColor();

    // The shadow deepens with the lift, so the card reads as held above the wall rather than sliding
    // across it, and shrinks back down as the drop settles.
    if (m_chrome.effects) {
        drawChromeRect(CBox{box.x - 4.0, box.y + 6.0 + lift * 10.0, box.w + 8.0, box.h + 8.0},
            withAlpha(Theme::shadowColor(), alpha * (0.28 + lift * 0.30)), damage, radius + 10);
        drawChromeRect(CBox{box.x + 6.0, box.y + 10.0 + lift * 6.0, box.w, box.h},
            withAlpha(Theme::shadowColor(), alpha * (0.44 + lift * 0.28)), damage, radius + 2);
    }
    drawChromeRect(box, surfaceColor(0.14F, alpha * 0.96), damage, radius);
    renderWindowPreview(window, box, alpha * 0.96, damage);
    drawSelectedBorder(box, withAlpha(accent, alpha * std::lerp(0.20, 0.74, lift)), radius, 1);
    drawSignalLock(rect, lift, accent, alpha, damage);

    // Named while it is in the air: a thumbnail alone is hard to identify at drag size, and the
    // chip is what makes the card feel picked up rather than smeared across the wall.
    if (box.w < 140.0)
        return;

    const auto chip = CBox{box.x + 10.0, box.y + box.h - 32.0 + (1.0 - lift) * 6.0, std::max(1.0, box.w - 20.0), 24.0};
    drawChromeRect(chip, withAlpha(tintedSurface(surfaceColor(0.08F, 1.0), accent, 0.16), alpha * 0.86 * lift), damage, 8);
    m_labels.renderColored(appGlyph(window.appClass), chip.x + 9.0, chip.y + 5.0, 18.0, Theme::hintSize(), accent, alpha * lift, damage);
    m_labels.render(window.label, chip.x + 30.0, chip.y + 5.0, std::max(1.0, chip.w - 40.0), Theme::hintSize(), alpha * 0.86 * lift, damage);
}

void OverlayRenderer::renderSearchPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    if (!m_searchActive)
        return;

    const auto suggestions    = matchingSearchSuggestions();
    std::vector<OverviewTarget> targets;
    targets.reserve(suggestions.size());
    for (const auto& suggestion : suggestions)
        targets.push_back(suggestion.target);

    const auto geometry       = computeSearchPanelGeometry(frame, targets.size());
    const auto visibleStart   = visibleSearchStart(targets, m_selectedTarget, geometry.capacity);
    const auto visibleEnd     = std::min(targets.size(), visibleStart + geometry.capacity);

    const auto accent     = resolvedAccentColor();
    const auto background = m_config.backgroundColor();
    const auto foreground = m_config.foregroundColor();

    const auto panelBox = CBox{geometry.panelX, geometry.panelY, geometry.panelW, geometry.panelH};
    const auto inputBox = CBox{geometry.inputX, geometry.inputY, geometry.inputW, geometry.inputH};

    if (m_chrome.effects)
        drawChromeRect(CBox{panelBox.x + Theme::shadowOffsetX(), panelBox.y + Theme::shadowOffsetY(), panelBox.w, panelBox.h},
            withAlpha(Theme::shadowColor(), alpha), damage, Theme::searchRadius());
    drawChromeRect(panelBox, withAlpha(tintedSurface(Theme::searchPanelColor(), background, 0.34), alpha), damage, Theme::searchRadius(), true);

    drawSelectedBorder(panelBox, withAlpha(accent, alpha * 0.62), Theme::searchRadius(), 1);

    drawChromeRect(inputBox, withAlpha(tintedSurface(Theme::searchInputColor(), background, 0.28), alpha), damage, Theme::inputRadius());

    // Measure through the shared cache rather than a second hand-rolled lookup with its own key
    // format: the old IIFE inserted a differently-keyed entry for the same ">" the renderLabel below
    // already caches.
    const auto promptSize   = m_labels.measure(">", 32.0, Theme::labelSize(), m_config.foregroundColor());
    const auto promptWidth  = promptSize.width;
    const auto promptHeight = promptSize.height;
    const auto textY        = inputBox.y + std::round((inputBox.h - promptHeight) / 2.0);
    const auto promptX      = inputBox.x + 16.0;
    const auto queryX       = promptX + promptWidth + 10.0;

    m_labels.renderColored(">", promptX, textY, 32.0, Theme::labelSize(), accent, alpha, damage);
    const auto queryText = m_searchQuery.empty() ? "apps, windows, workspaces_" : std::format("{}_", m_searchQuery);
    m_labels.renderColored(queryText, queryX, textY,
        std::max(1.0, inputBox.x + inputBox.w - 106.0 - queryX), Theme::labelSize(), foreground,
        alpha * (m_searchQuery.empty() ? 0.38 : 1.0), damage);
    m_labels.renderColored(std::format("{:02} FOUND", targets.size()),
        inputBox.x + inputBox.w - 84.0, inputBox.y + 17.0, 72.0, Theme::hintSize(), foreground, alpha * 0.50, damage);

    for (std::size_t index = visibleStart; index < visibleEnd; ++index) {
        const auto& suggestion = suggestions[index];
        const auto& target  = suggestion.target;
        const auto selected = sameTarget(target, m_selectedTarget);
        const auto rowIndex = index - visibleStart;
        const auto row      = CBox{
            inputBox.x,
            geometry.resultsY + static_cast<double>(rowIndex) * (geometry.rowHeight + geometry.rowGap),
            inputBox.w,
            geometry.rowHeight,
        };

        drawChromeRect(row, selected ? withAlpha(accent, alpha * 0.18) : Theme::searchRowFill(false, static_cast<float>(alpha)), damage, Theme::inputRadius());
        if (selected) {
            const auto accentHeight = std::max(1.0, row.h - 20.0);
            drawRect(CBox{row.x, row.y + (row.h - accentHeight) / 2.0, 4.0, accentHeight},
                withAlpha(accent, alpha), damage, 2);
        }

        auto typeLabel   = "WS";
        auto actionLabel = "SWITCH";
        if (suggestion.kind == SearchSuggestionKind::Application) {
            typeLabel   = "APP";
            actionLabel = "OPEN";
        } else if (suggestion.kind == SearchSuggestionKind::Window) {
            typeLabel   = "WIN";
            actionLabel = "FOCUS";
        }
        const auto glyph = suggestion.kind == SearchSuggestionKind::Workspace ?
            std::format("#{}", target.workspaceId) : appGlyph(suggestion.appClass);
        const auto textX = row.x + 76.0;

        m_labels.renderColored(typeLabel, row.x + 18.0, row.y + 8.0, 38.0, Theme::badgeSize(),
            selected ? accent : foreground, alpha * (selected ? 0.94 : 0.48), damage);
        m_labels.renderColored(glyph, row.x + 18.0, row.y + 25.0, 42.0, Theme::labelSize(),
            selected ? accent : foreground, alpha * (selected ? 1.0 : 0.72), damage);
        drawRect(CBox{row.x + 58.0, row.y + 10.0, 1.0, row.h - 20.0},
            withAlpha(selected ? accent : foreground, alpha * (selected ? 0.42 : 0.11)), damage);

        m_labels.renderColored(suggestion.label, textX, row.y + 8.0, std::max(1.0, row.w - 184.0),
            Theme::labelSize(), foreground, alpha * (selected ? 1.0 : 0.88), damage);
        m_labels.renderColored(suggestion.context, textX, row.y + 31.0, std::max(1.0, row.w - 184.0),
            Theme::hintSize(), foreground, alpha * (selected ? 0.58 : 0.42), damage);
        m_labels.renderColored(actionLabel, row.x + row.w - 78.0, row.y + 22.0, 62.0,
            Theme::badgeSize(), selected ? accent : foreground, alpha * (selected ? 0.88 : 0.28), damage);
    }

    if (targets.empty()) {
        const auto emptyBox = CBox{inputBox.x, geometry.resultsY, inputBox.w, 64.0};
        drawChromeRect(emptyBox, Theme::searchRowFill(false, static_cast<float>(alpha)), damage, Theme::inputRadius());
        drawRect(CBox{emptyBox.x, emptyBox.y + 12.0, 3.0, emptyBox.h - 24.0}, withAlpha(accent, alpha * 0.42), damage, 2);
        m_labels.renderColored("NO MATCHES", emptyBox.x + 18.0, emptyBox.y + 12.0,
            emptyBox.w - 36.0, Theme::labelSize(), accent, alpha * 0.78, damage);
        m_labels.renderColored("try an app, window title, or workspace", emptyBox.x + 18.0, emptyBox.y + 37.0,
            emptyBox.w - 36.0, Theme::hintSize(), foreground, alpha * 0.48, damage);
    }

    m_labels.renderColored("\xe2\x86\x91\xe2\x86\x93 select \xc2\xb7 Enter open \xc2\xb7 Esc clear",
        panelBox.x + 28.0, panelBox.y + panelBox.h - 24.0,
        panelBox.w - 56.0, Theme::hintSize(), foreground, alpha * 0.48, damage);
}

OverviewTarget OverlayRenderer::searchTargetAt(const WorkspaceWallFrame& frame, double x, double y) const {
    const auto targets = matchingSearchTargets();
    if (targets.empty())
        return {};

    const auto geometry     = computeSearchPanelGeometry(frame, targets.size());
    const auto visibleStart = visibleSearchStart(targets, m_selectedTarget, geometry.capacity);
    const auto visibleEnd   = std::min(targets.size(), visibleStart + geometry.capacity);

    for (std::size_t index = visibleStart; index < visibleEnd; ++index) {
        const auto rowIndex = index - visibleStart;
        const auto row      = LayoutRect{
            .x      = geometry.inputX,
            .y      = geometry.resultsY + static_cast<double>(rowIndex) * (geometry.rowHeight + geometry.rowGap),
            .width  = geometry.inputW,
            .height = geometry.rowHeight,
        };
        if (contains(row, x, y))
            return targets[index];
    }

    return {};
}

const WindowCard* OverlayRenderer::findWindowCard(std::uint64_t windowId) const noexcept {
    const auto byId = [windowId](const WindowCard& card) { return card.stableId == windowId; };
    for (const auto& frame : m_frames) {
        if (const auto it = std::ranges::find_if(frame.stage.windows, byId); it != frame.stage.windows.end())
            return &*it;
        for (const auto& workspace : frame.workspaces) {
            if (const auto it = std::ranges::find_if(workspace.windows, byId); it != workspace.windows.end())
                return &*it;
        }
    }

    return nullptr;
}

const WorkspaceCard* OverlayRenderer::findWorkspaceCard(std::int64_t workspaceId) const noexcept {
    for (const auto& frame : m_frames) {
        const auto it = std::ranges::find(frame.workspaces, workspaceId, &WorkspaceCard::workspaceId);
        if (it != frame.workspaces.end())
            return &*it;
    }

    return nullptr;
}

const WorkspaceWallFrame* OverlayRenderer::frameForMonitor(std::int64_t monitorId) const noexcept {
    const auto found = std::ranges::find_if(m_frames, [monitorId](const WorkspaceWallFrame& frame) { return frame.monitorId == monitorId; });
    return found == m_frames.end() ? nullptr : &*found;
}

const WorkspaceWallFrame* OverlayRenderer::frameForPoint(double x, double y, double& localX, double& localY) const noexcept {
    for (const auto& frame : m_frames) {
        const auto bounds = m_frameBoundsByMonitor.find(frame.monitorId);
        if (bounds == m_frameBoundsByMonitor.end())
            continue;

        if (!contains(bounds->second, x, y))
            continue;

        const auto local = mapGlobalPointToFrame(bounds->second, frame.bounds, x, y);
        localX = local.x;
        localY = local.y;
        return &frame;
    }

    const WorkspaceWallFrame* localHit = nullptr;
    for (const auto& frame : m_frames) {
        if (!contains(frame.bounds, x, y))
            continue;

        if (localHit) {
            localX = x;
            localY = y;
            return activeMonitorFrame();
        }

        localHit = &frame;
    }

    if (localHit) {
        localX = x;
        localY = y;
    }

    return localHit;
}

const WorkspaceWallFrame* OverlayRenderer::frameForSelectedTarget() const noexcept {
    if (const auto* selectedMonitorFrame = frameForMonitor(m_selectedFrameMonitorId))
        return selectedMonitorFrame;

    const WorkspaceWallFrame* matchingFrame = nullptr;
    for (const auto& frame : m_frames) {
        if (!targetInFrame(frame, m_selectedTarget))
            continue;

        if (matchingFrame)
            return activeMonitorFrame();

        matchingFrame = &frame;
    }

    return matchingFrame ? matchingFrame : activeMonitorFrame();
}

const WorkspaceWallFrame* OverlayRenderer::activeMonitorFrame() const noexcept {
    if (g_pCompositor) {
        if (const auto monitor = HyprlandCompat::monitorFromCursor()) {
            if (const auto* frame = frameForMonitor(monitor->m_id))
                return frame;
        }
    }

    if (m_frames.empty())
        return nullptr;

    return &m_frames.front();
}

PreferenceHit OverlayRenderer::preferenceControlAt(double x, double y) const {
    if (!m_preferencesVisible)
        return {};

    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame || frame->monitorId != m_preferencesMonitorId)
        return {};
    return hitTestPreferencesPanel(computePreferencesPanel(
        frame->bounds, effectiveLayoutMode() == LayoutMode::Stage, nativeThemeOptionCount()), localX, localY);
}

bool OverlayRenderer::pointerInsidePreferencesPanel(double x, double y) const {
    if (!m_preferencesVisible)
        return false;

    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame || frame->monitorId != m_preferencesMonitorId)
        return false;
    return containsPreferencesPanel(computePreferencesPanel(
        frame->bounds, effectiveLayoutMode() == LayoutMode::Stage, nativeThemeOptionCount()), localX, localY);
}

PointerAction OverlayRenderer::applyPreference(PreferenceControl control, int value, int step) {
    if (control == PreferenceControl::None)
        return {};
    if (control == PreferenceControl::Close) {
        togglePreferences();
        return {};
    }
    if (control == PreferenceControl::AppExpose) {
        m_preferencesVisible = false;
        m_preferencesMonitorId = -1;
        damageAllMonitors();
        return {.type = PointerActionType::ShowAppExpose, .target = {}, .windowId = 0};
    }

    auto& state = m_preferences.state();
    const auto adjacent = [step](int current, int count) {
        return ((current + step) % count + count) % count;
    };
    switch (control) {
    case PreferenceControl::WorkspaceView:
        if (value == 0)
            state.workspaceView = WorkspaceViewPreference::Stage;
        else if (value == 1)
            state.workspaceView = WorkspaceViewPreference::WorkspaceWall;
        else if (value == 2)
            state.workspaceView = WorkspaceViewPreference::Carousel;
        else if (value == 3)
            state.workspaceView = WorkspaceViewPreference::Ribbon;
        else {
            auto current = 0;
            if (effectiveLayoutMode() == LayoutMode::WorkspaceWall)
                current = 1;
            else if (effectiveLayoutMode() == LayoutMode::Carousel)
                current = 2;
            else if (effectiveLayoutMode() == LayoutMode::Ribbon)
                current = 3;
            state.workspaceView = static_cast<WorkspaceViewPreference>(adjacent(current, 4) + 1);
        }
        break;
    case PreferenceControl::WindowView:
        if (value == 0)
            state.windowView = WindowViewPreference::Spatial;
        else if (value == 1)
            state.windowView = WindowViewPreference::Grouped;
        else if (value == 2)
            state.windowView = WindowViewPreference::Deck;
        else
            state.windowView = static_cast<WindowViewPreference>(adjacent(static_cast<int>(state.windowView), 3));
        break;
    case PreferenceControl::Shelf:
        if (value >= 0 && value <= 3)
            state.shelf = static_cast<ShelfPreference>(value);
        else
            state.shelf = static_cast<ShelfPreference>(adjacent(static_cast<int>(state.shelf), 4));
        break;
    case PreferenceControl::Motion:
        if (value >= 0 && value <= 6)
            state.motion = static_cast<MotionPreference>(value);
        else
            state.motion = static_cast<MotionPreference>(adjacent(static_cast<int>(state.motion), 7));
        break;
    case PreferenceControl::Chrome:
        if (value >= 0 && value <= 3)
            state.chrome = static_cast<ChromePreference>(value);
        else
            state.chrome = static_cast<ChromePreference>(adjacent(static_cast<int>(state.chrome), 4));
        break;
    case PreferenceControl::NativeTheme: {
        const auto count = nativeThemeOptionCount();
        if (count <= 0)
            return {};
        if (value == 1)
            return {};
        const auto direction = value == 0 ? -1 : value == 2 ? 1 : step;
        const auto current = selectedNativeThemeIndex();
        const auto selected = ((current + direction) % count + count) % count;
        state.nativeTheme = selected == 0 ? std::string{} : m_installedThemes[static_cast<std::size_t>(selected - 1)].slug;
        m_config.refreshPalette(state.nativeTheme);
        break;
    }
    case PreferenceControl::None:
    case PreferenceControl::AppExpose:
    case PreferenceControl::Close:
        return {};
    }

    if (!m_preferences.save())
        log::warn("could not save preferences to {}", m_preferences.path().string());
    rebuildAfterPreferenceChange();
    return {};
}

int OverlayRenderer::selectedNativeThemeIndex() const noexcept {
    if (m_preferences.state().nativeTheme.empty())
        return 0;

    const auto selected = std::ranges::find(m_installedThemes, m_preferences.state().nativeTheme, &OmarchyTheme::slug);
    return selected == m_installedThemes.end() ? 0 : static_cast<int>(std::distance(m_installedThemes.begin(), selected)) + 1;
}

int OverlayRenderer::nativeThemeOptionCount() const noexcept {
    return static_cast<int>(m_installedThemes.size()) + 1;
}

void OverlayRenderer::rebuildAfterPreferenceChange() {
    applyMotionProfile();
    refreshChromeStyle();
    m_mode = defaultOverviewMode();
    m_applicationFilter.clear();
    m_previousFrames = m_frames;
    rebuildFrames();
    normalizeShelfVisibility();
    if (const auto* frame = frameForMonitor(m_selectedFrameMonitorId)) {
        if (!targetInFrame(*frame, m_selectedTarget))
            m_selectedTarget = m_hitTester.initialSelection(*frame);
    } else if (const auto* frame = activeMonitorFrame()) {
        m_selectedFrameMonitorId = frame->monitorId;
        m_selectedTarget = m_hitTester.initialSelection(*frame);
    }
    m_stageTransitionMonitorId = -1;
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, effectiveAnimationDurationMs());
    animateSelection();
    m_labels.clear();
    damageAllMonitors();
}

CHyprColor OverlayRenderer::resolvedAccentColor() const {
    const auto& accent = m_config.palette().accent;
    return {accent.red, accent.green, accent.blue, accent.alpha};
}

LayoutMode OverlayRenderer::effectiveLayoutMode() const {
    switch (m_preferences.state().workspaceView) {
    case WorkspaceViewPreference::Stage:
        return LayoutMode::Stage;
    case WorkspaceViewPreference::WorkspaceWall:
        return LayoutMode::WorkspaceWall;
    case WorkspaceViewPreference::Carousel:
        return LayoutMode::Carousel;
    case WorkspaceViewPreference::Ribbon:
        return LayoutMode::Ribbon;
    case WorkspaceViewPreference::FollowConfig:
        return m_config.layoutMode();
    }
    return m_config.layoutMode();
}

ChromePreset OverlayRenderer::effectiveChromePreset() const {
    switch (m_preferences.state().chrome) {
    case ChromePreference::Radiant: return ChromePreset::Radiant;
    case ChromePreference::Native: return ChromePreset::Native;
    case ChromePreference::Flat: return ChromePreset::Flat;
    case ChromePreference::FollowConfig: return m_config.chromePreset();
    }
    return m_config.chromePreset();
}

ShelfMode OverlayRenderer::effectiveShelfMode() const {
    switch (m_preferences.state().shelf) {
    case ShelfPreference::Auto: return ShelfMode::Auto;
    case ShelfPreference::Always: return ShelfMode::Always;
    case ShelfPreference::Hidden: return ShelfMode::Hidden;
    case ShelfPreference::FollowConfig: return m_config.shelfMode();
    }
    return m_config.shelfMode();
}

bool OverlayRenderer::shelfAutomationAllowed(bool visible) const {
    const auto mode = effectiveShelfMode();
    return mode == ShelfMode::Auto || (mode == ShelfMode::Always && visible) || (mode == ShelfMode::Hidden && !visible);
}

void OverlayRenderer::normalizeShelfVisibility() {
    if (effectiveLayoutMode() != LayoutMode::Stage || !active())
        return;
    if (effectiveShelfMode() == ShelfMode::Always)
        setWorkspaceShelfVisible(true, true);
    else if (effectiveShelfMode() == ShelfMode::Hidden)
        setWorkspaceShelfVisible(false, true);
}

int OverlayRenderer::effectiveAnimationDurationMs() const {
    const auto configured = m_config.animationDurationMs();
    auto duration = configured;
    switch (m_preferences.state().motion) {
    case MotionPreference::Quattro:
        duration = std::min(2000, static_cast<int>(std::round(configured * 0.82)));
        break;
    case MotionPreference::Cyberpunk:
        duration = std::min(2000, static_cast<int>(std::round(configured * 1.22)));
        break;
    case MotionPreference::Tron:
        duration = std::min(2000, static_cast<int>(std::round(configured * 1.65)));
        break;
    case MotionPreference::Elegant:
        duration = std::min(2000, static_cast<int>(std::round(configured * 2.05)));
        break;
    case MotionPreference::Reduced:
        duration = std::min(configured, 90);
        break;
    case MotionPreference::Off:
        duration = 0;
        break;
    case MotionPreference::FollowConfig:
        break;
    }
    return effectiveLayoutMode() == LayoutMode::Ribbon ? std::min(duration, RIBBON_DURATION_CAP_MS) : duration;
}

AnimationCurve OverlayRenderer::effectiveAnimationCurve() const {
    switch (m_preferences.state().motion) {
    case MotionPreference::Quattro:
        return AnimationCurve::Quattro;
    case MotionPreference::Cyberpunk:
        return AnimationCurve::Cyberpunk;
    case MotionPreference::Tron:
        return AnimationCurve::Tron;
    case MotionPreference::Elegant:
        return AnimationCurve::Elegant;
    case MotionPreference::FollowConfig:
    case MotionPreference::Reduced:
    case MotionPreference::Off:
        return AnimationCurve::Smooth;
    }
    return AnimationCurve::Smooth;
}

void OverlayRenderer::applyMotionProfile() {
    const auto curve = effectiveAnimationCurve();
    const std::array animations{
        &m_animation,
        &m_stageTransition,
        &m_selectionTransition,
        &m_shelfTransition,
        &m_dockTransition,
        &m_closeButtonTransition,
        &m_closeButtonHotTransition,
        &m_windowCloseTransition,
        &m_pressTransition,
        &m_dragLiftTransition,
        &m_dropTargetTransition,
        &m_dragSettleTransition,
    };
    for (auto* animation : animations)
        animation->setCurve(curve);
}

OverviewMode OverlayRenderer::defaultOverviewMode() const {
    switch (m_preferences.state().windowView) {
    case WindowViewPreference::Grouped:
        return OverviewMode::Grouped;
    case WindowViewPreference::Deck:
        return OverviewMode::Deck;
    case WindowViewPreference::Spatial:
        return OverviewMode::Spatial;
    }
    return OverviewMode::Spatial;
}

CHyprColor OverlayRenderer::surfaceColor(float lift, double alpha) const {
    const auto& palette = m_config.palette();
    const auto  lifted  = liftedSurface(palette, palette.background, lift);
    return {lifted.red, lifted.green, lifted.blue, static_cast<float>(std::clamp(alpha, 0.0, 1.0))};
}

void OverlayRenderer::damageMonitorById(std::int64_t monitorId) const {
    if (!g_pCompositor || !g_pHyprRenderer)
        return;

    for (const auto& monitor : HyprlandCompat::monitors()) {
        if (!monitor || monitor->m_id != monitorId || !HyprlandCompat::monitorExists(monitor))
            continue;

        g_pHyprRenderer->damageMonitor(monitor);
        HyprlandCompat::scheduleFrame(monitor);
        return;
    }
}

void OverlayRenderer::damageAllMonitors() const {
    if (!g_pCompositor || !g_pHyprRenderer)
        return;

    for (const auto& monitor : HyprlandCompat::monitors()) {
        if (!HyprlandCompat::monitorExists(monitor))
            continue;

        g_pHyprRenderer->damageMonitor(monitor);
        HyprlandCompat::scheduleFrame(monitor);
    }
}

} // namespace hypr_radiant
