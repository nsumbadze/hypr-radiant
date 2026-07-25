#include <hypr-radiant/overview/OverlayGeometry.hpp>
#include <hypr-radiant/render/OverlayRenderer.hpp>
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
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hypr_radiant {
namespace {

// Multiplier applied to the configured animation duration for the workspace depth push.
constexpr auto WORKSPACE_PUSH_SCALE = 1.3;
// Band along the bottom edge the hint dock counts as its own. The pointer has to leave this
// entirely before the dock retracts, so a small twitch over the dock does not dismiss it.
constexpr auto DOCK_BAND_HEIGHT = 92.0;
// Short enough to feel attached to the pointer rather than played back at it.
constexpr auto CLOSE_REVEAL_MS = 140;
constexpr auto CLOSE_HOT_MS    = 110;

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

CHyprColor withAlpha(CHyprColor color, double multiplier) {
    color.a *= multiplier;
    return color;
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
            if (target.type == OverviewTargetType::Window && window.stableId == target.windowId)
                return true;
        }
    }

    for (const auto& window : frame.stage.windows) {
        if (target.type == OverviewTargetType::Window && window.stableId == target.windowId)
            return true;
    }

    return false;
}

PHLWINDOW findLiveWindow(std::uint64_t stableId) {
    if (!g_pCompositor)
        return nullptr;

    const auto it = std::ranges::find_if(g_pCompositor->m_windows,
        [stableId](const PHLWINDOW& window) { return window && window->m_stableID == stableId && window->m_isMapped; });
    return it == g_pCompositor->m_windows.end() ? nullptr : *it;
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

OverlayRenderer::OverlayRenderer(const RadiantConfig& config) : m_config(config), m_labels(config) {}

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
    m_mode              = mode;
    m_applicationFilter = std::move(applicationFilter);
    m_state             = std::move(state);
    resetPointerInteraction();
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
    releaseHoverAffordances();
    m_animation.animateTo(true, m_config.animationDurationMs());
    m_stageTransitionMonitorId = -1;
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, stageDurationMs);
    animateSelection();
    damageAllMonitors();
}

void OverlayRenderer::show(RadiantState state) {
    beginSession(std::move(state), OverviewMode::Spatial, {},
        std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * 0.78))),
        [this](const WorkspaceWallFrame& frame) { return m_hitTester.initialSelection(frame); });
}

void OverlayRenderer::showAppExpose(RadiantState state, std::string applicationClass) {
    beginSession(std::move(state), OverviewMode::AppExpose, std::move(applicationClass), m_config.animationDurationMs(),
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
        m_animation.animateTo(false, std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * 0.67))));
        damageAllMonitors();
        return;
    }

    show(std::move(state));
}

void OverlayRenderer::moveSelection(NavigationDirection direction) {
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
    m_selectedTarget = m_hitTester.moveSelection(*frame, m_selectedTarget, direction);
    m_selectedFrameMonitorId = frameMonitorId;
    if (!sameTarget(previousTarget, m_selectedTarget))
        animateSelection();
    if (m_config.layoutMode() == LayoutMode::Stage && m_selectedTarget.workspaceId != previousWorkspace) {
        m_previousFrames = m_frames;
        rebuildFrames();
        m_stageTransitionMonitorId = frameMonitorId;
        m_stageTransition.hideImmediate();
        // Longer than the open animation: the depth push needs room to read as movement.
        m_stageTransition.animateTo(true, std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * WORKSPACE_PUSH_SCALE))));
    }
    damageMonitorById(frameMonitorId);
}

void OverlayRenderer::selectTargetAt(double x, double y) {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return;

    if (!m_searchActive && m_config.layoutMode() == LayoutMode::Stage) {
        const auto mapped = mapDisplayedStagePoint(*frame, {.x = localX, .y = localY}, workspaceShelfVisible());
        localX = mapped.x;
        localY = mapped.y;
    }
    auto target = !m_searchActive ? m_hitTester.hitTest(*frame, localX, localY) : searchTargetAt(*frame, localX, localY);
    if (!workspaceShelfVisible() && !m_searchActive && target.type == OverviewTargetType::Workspace && localY < frame->stage.bounds.y)
        target = {};
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
    if (!m_searchActive && m_config.layoutMode() == LayoutMode::Stage && target.workspaceId != previousWorkspace) {
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
            // Longer than the open animation: the depth push needs room to read as movement.
            m_stageTransition.animateTo(true, std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * WORKSPACE_PUSH_SCALE))));
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
    if (!m_searchActive && m_config.layoutMode() == LayoutMode::Stage) {
        double localX = x;
        double localY = y;
        if (const auto* frame = frameForPoint(x, y, localX, localY)) {
            constexpr auto revealEdge = 12.0;
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
        m_dragging = true;

    if (!m_dragging)
        return;

    const auto target = hitTest(x, y);
    m_dragTarget = target.type == OverviewTargetType::Workspace || target.type == OverviewTargetType::NewWorkspace ? target : OverviewTarget{};
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
        m_pointerDownTarget = hitTest(x, y);
        m_dragTarget = {};
        return {};
    }

    if (!m_pointerDown)
        return {};

    pointerMoved(x, y);
    PointerAction action;
    if (m_dragging && m_dragTarget.type != OverviewTargetType::None) {
        action = {
            .type = m_dragTarget.type == OverviewTargetType::NewWorkspace ? PointerActionType::CreateWorkspaceAndMoveWindow : PointerActionType::MoveWindow,
            .target = m_dragTarget,
            .windowId = m_pointerDownTarget.windowId,
        };
    } else if (!m_dragging) {
        const auto releasedTarget = hitTest(x, y);
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
    m_stageTransition.animateTo(true, m_config.animationDurationMs());
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
    m_animation.animateTo(visible, m_config.animationDurationMs());
    m_stageTransition.animateTo(visible, m_config.animationDurationMs());
    damageAllMonitors();
}

void OverlayRenderer::appendSearchChar(char value) {
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
    if (m_searchActive)
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
    if (m_config.layoutMode() != LayoutMode::Stage || m_searchActive)
        return;

    m_mode = m_mode == OverviewMode::Grouped ? OverviewMode::Spatial : OverviewMode::Grouped;
    m_applicationFilter.clear();
    m_previousFrames = m_frames;
    rebuildFrames();
    if (const auto* frame = frameForMonitor(m_selectedFrameMonitorId)) {
        if (!targetInFrame(*frame, m_selectedTarget))
            m_selectedTarget = m_hitTester.initialSelection(*frame);
    }
    m_stageTransitionMonitorId = -1;
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, m_config.animationDurationMs());
    animateSelection();
    m_labels.clear();
    damageAllMonitors();
}

void OverlayRenderer::setWorkspaceShelfVisible(bool visible) {
    if (m_config.layoutMode() != LayoutMode::Stage || !active() || m_shelfTransition.targetVisible() == visible)
        return;

    m_shelfTransition.animateTo(visible, std::max(90, static_cast<int>(std::round(m_config.animationDurationMs() * 0.82))));
    damageAllMonitors();
}

void OverlayRenderer::updateCloseAffordance(double x, double y) {
    std::uint64_t windowId = 0;
    auto          hot      = false;
    if (!m_dragging && !m_searchActive && m_config.layoutMode() == LayoutMode::Stage && active()) {
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
        setPointerCursorOverride(hot);
        damageMonitorById(m_selectedFrameMonitorId);
    }
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
    if (m_config.layoutMode() != LayoutMode::Stage || !active() || m_dockTransition.targetVisible() == visible)
        return;

    m_dockTransition.animateTo(visible, std::max(90, static_cast<int>(std::round(m_config.animationDurationMs() * 0.82))));
    damageAllMonitors();
}

void OverlayRenderer::toggleWorkspaceShelf() {
    setWorkspaceShelfVisible(!m_shelfTransition.targetVisible());
}

void OverlayRenderer::setWorkspaceShelfGestureProgress(bool revealing, double progress) {
    if (m_config.layoutMode() != LayoutMode::Stage || !active())
        return;

    m_shelfTransition.setProgress(revealing ? progress : 1.0 - progress, revealing);
    damageAllMonitors();
}

void OverlayRenderer::finishWorkspaceShelfGesture(bool revealing, bool commit) {
    const auto visible = revealing ? commit : !commit;
    m_shelfTransition.animateTo(visible, std::max(90, static_cast<int>(std::round(m_config.animationDurationMs() * 0.72))));
    damageAllMonitors();
}

void OverlayRenderer::hideImmediate() {
    const auto wasRenderable = m_animation.renderable();
    m_animation.hideImmediate();
    m_stageTransition.hideImmediate();
    m_selectionTransition.hideImmediate();
    m_windowCloseTransition.hideImmediate();
    m_shelfTransition.hideImmediate();
    m_dockTransition.hideImmediate();
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
}

void OverlayRenderer::animateSelection() {
    m_selectionTransition.hideImmediate();
    m_selectionTransition.animateTo(true, std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * 0.62))));
}

bool OverlayRenderer::active() const noexcept {
    return m_animation.targetVisible();
}

bool OverlayRenderer::searchActive() const noexcept {
    return m_searchActive;
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

OverviewTarget OverlayRenderer::hitTest(double x, double y) const {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return {};

    if (!m_searchActive && m_config.layoutMode() == LayoutMode::Stage) {
        const auto mapped = mapDisplayedStagePoint(*frame, {.x = localX, .y = localY}, workspaceShelfVisible());
        localX = mapped.x;
        localY = mapped.y;
    }
    auto target = !m_searchActive ? m_hitTester.hitTest(*frame, localX, localY) : searchTargetAt(*frame, localX, localY);
    if (!workspaceShelfVisible() && !m_searchActive && target.type == OverviewTargetType::Workspace && localY < frame->stage.bounds.y)
        return {};
    return target;
}

void OverlayRenderer::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_LAST_MOMENT || !m_animation.renderable())
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
    if (m_animation.running() || m_shelfTransition.running() || m_dockTransition.running()) {
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

    if (!monitor || !g_pCompositor->monitorExists(monitor))
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
    auto backdrop = m_config.layoutMode() == LayoutMode::Stage ? surfaceColor(0.06F, 0.70) : Theme::backdropColor();
    backdrop.a *= backdropAlpha;
    drawRect(box, backdrop, damage, 0, true);

    const auto* frame = frameForMonitor(monitor->m_id);
    if (!frame)
        return;

    const auto sizeChanged = std::abs(frame->bounds.width - width) > 1.0 || std::abs(frame->bounds.height - height) > 1.0;
    if (sizeChanged)
        return;

    renderFrame(*frame, alpha, damage);
}

void OverlayRenderer::rebuildFrames() {
    m_frames.clear();
    m_frameBoundsByMonitor.clear();

    if (g_pCompositor) {
        for (const auto& monitor : g_pCompositor->m_monitors) {
            if (!monitor || !g_pCompositor->monitorExists(monitor))
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
                layoutOptionsFor(m_config.layoutMode(), previewWorkspaceId, m_mode, m_applicationFilter)));
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
                layoutOptionsFor(m_config.layoutMode(), previewWorkspaceId, m_mode, m_applicationFilter)));
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

    for (const auto& frame : m_frames) {
        if (m_mode != OverviewMode::Spatial) {
            if (frame.monitorId != m_selectedFrameMonitorId)
                continue;
            const auto matches = m_searchMatcher.matchingStageWindowIds(frame, m_searchQuery);
            m_searchMatches.insert(matches.begin(), matches.end());
            continue;
        }
        if (m_searchQuery.empty()) {
            for (const auto& workspace : frame.workspaces) {
                for (const auto& window : workspace.windows)
                    m_searchMatches.insert(window.stableId);
            }
            continue;
        }
        const auto matches = m_searchMatcher.matchingWindowIds(frame, m_searchQuery);
        m_searchMatches.insert(matches.begin(), matches.end());
    }
}

void OverlayRenderer::selectFirstSearchMatch() {
    if (!m_searchActive)
        return;

    const auto targets = matchingSearchTargets();
    if (!targets.empty()) {
        m_selectedTarget = targets.front();
        if (const auto* frame = frameForSelectedTarget())
            m_selectedFrameMonitorId = frame->monitorId;
        return;
    }

    m_selectedTarget = {};
}

std::vector<OverviewTarget> OverlayRenderer::matchingSearchTargets() const {
    std::vector<OverviewTarget> targets;
    if (!m_searchActive)
        return targets;

    std::unordered_set<std::int64_t> workspaceIds;
    std::unordered_set<std::uint64_t> windowIds;
    for (const auto& frame : m_frames) {
        if (m_mode != OverviewMode::Spatial) {
            if (frame.monitorId != m_selectedFrameMonitorId)
                continue;
            for (const auto& window : frame.stage.windows) {
                if (m_searchMatches.contains(window.stableId) && !windowIds.contains(window.stableId)) {
                    targets.push_back({.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
                    windowIds.insert(window.stableId);
                }
            }
            continue;
        }
        for (const auto& workspace : frame.workspaces) {
            if (workspace.createTarget)
                continue;
            const auto workspaceNumber = std::to_string(workspace.workspaceId);
            if (!workspaceIds.contains(workspace.workspaceId) &&
                (m_searchQuery.empty() || m_searchMatcher.matches(workspace.name, m_searchQuery) || m_searchMatcher.matches(workspaceNumber, m_searchQuery))) {
                targets.push_back({.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId});
                workspaceIds.insert(workspace.workspaceId);
            }

            for (const auto& window : workspace.windows) {
                if (m_searchMatches.contains(window.stableId) && !windowIds.contains(window.stableId)) {
                    targets.push_back({.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
                    windowIds.insert(window.stableId);
                }
            }
        }
    }

    return targets;
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
    if (const auto* frame = frameForSelectedTarget())
        m_selectedFrameMonitorId = frame->monitorId;
    damageAllMonitors();
}

void OverlayRenderer::clearSearch() {
    m_searchQuery.clear();
    m_searchMatches.clear();
    m_searchActive = false;
}

void OverlayRenderer::renderFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    const auto mode         = m_config.layoutMode();
    if (mode == LayoutMode::Stage) {
        renderStageFrame(frame, alpha, damage);
        return;
    }

    const auto searchActive = m_searchActive;
    const auto contentAlpha = searchActive ? alpha * 0.055 : alpha;

    const auto shadowColor = withAlpha(Theme::shadowColor(), contentAlpha);

    double contentLeft   = frame.bounds.width;
    double contentRight  = 0.0;
    double contentTop    = frame.bounds.height;
    double contentBottom = 0.0;
    bool   hasContent    = false;
    for (const auto& workspace : frame.workspaces) {
        if (workspace.rect.width <= 0.0 || workspace.rect.height <= 0.0)
            continue;
        contentLeft   = std::min(contentLeft, workspace.rect.x);
        contentRight  = std::max(contentRight, workspace.rect.x + workspace.rect.width);
        contentTop    = std::min(contentTop, workspace.rect.y);
        contentBottom = std::max(contentBottom, workspace.rect.y + workspace.rect.height);
        hasContent    = true;
    }

    const auto titleX = hasContent ? contentLeft : 46.0;
    const auto titleY = hasContent ? std::max(34.0, contentTop - 58.0) : 34.0;
    if (hasContent) {
        const auto stageBox = CBox{
            std::max(18.0, contentLeft - 44.0),
            std::max(18.0, titleY - 24.0),
            std::min(frame.bounds.width - 36.0, contentRight - contentLeft + 88.0),
            std::min(frame.bounds.height - 36.0, contentBottom - titleY + 62.0),
        };
        drawRect(CBox{stageBox.x + Theme::shadowOffsetX(), stageBox.y + Theme::shadowOffsetY(), stageBox.w, stageBox.h}, shadowColor, damage, 34);
        drawRect(stageBox, withAlpha(Theme::panelColor(), contentAlpha), damage, 32, true);
    }
    m_labels.render("Workspaces", titleX, titleY, std::max(1.0, frame.bounds.width * 0.45), Theme::titleSize(), contentAlpha, damage);
    if (!searchActive && hasContent) {
        const auto helpWidth = std::min(680.0, std::max(1.0, contentRight - contentLeft));
        const auto helpX     = contentLeft + centered(contentRight - contentLeft, helpWidth);
        m_labels.render("\xe2\x86\x91\xe2\x86\x93\xe2\x86\x90\xe2\x86\x92 navigate \xc2\xb7 Enter activate \xc2\xb7 1-9 jump \xc2\xb7 / search \xc2\xb7 Esc close",
            helpX, frame.bounds.height - 16.0 - 24.0 + 4.0, helpWidth, Theme::hintSize(), contentAlpha * 0.68, damage);
    }

    for (const auto& workspace : frame.workspaces) {
        const auto workspaceSelected = frame.monitorId == m_selectedFrameMonitorId &&
            sameTarget(m_selectedTarget, {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId});
        const auto workspaceBox = boxFor(workspace.rect);
        const auto compact      = workspace.rect.height <= 120.0;
        const auto round        = Theme::workspaceRadius(compact);

        drawRect(CBox{workspaceBox.x + Theme::shadowOffsetX(), workspaceBox.y + Theme::shadowOffsetY(), workspaceBox.w, workspaceBox.h}, shadowColor, damage, round);

        if (workspace.active && !compact) {
            const auto glowBox = CBox{workspaceBox.x - 6.0, workspaceBox.y - 6.0, workspaceBox.w + 12.0, workspaceBox.h + 12.0};
            drawRect(glowBox, withAlpha(Theme::activeGlowColor(), contentAlpha), damage, round + 6);
        }

        drawRect(workspaceBox, Theme::cardFill(workspace.active, workspaceSelected, workspace.empty, static_cast<float>(contentAlpha)), damage, round);

        if (workspaceSelected || workspace.active) {
            const auto borderColor = Theme::cardBorder(workspace.active, workspaceSelected, static_cast<float>(contentAlpha));
            drawBorder(workspaceBox, borderColor, round, 2);
        }

        if (workspace.active) {
            const auto accentWidth = std::min(compact ? 28.0 : 56.0, std::max(1.0, workspaceBox.w - 32.0));
            const auto accentBar   = withAlpha(Theme::accentColor(), contentAlpha);
            drawRect(CBox{workspaceBox.x + centered(workspaceBox.w, accentWidth), workspaceBox.y + workspaceBox.h - 7.0, accentWidth, 4.0},
                accentBar, damage, 2);
        }

        m_labels.render(workspace.name, workspace.rect.x + (compact ? 10.0 : 18.0), workspace.rect.y + (compact ? 8.0 : 14.0),
            std::max(1.0, workspace.rect.width - (compact ? 20.0 : 36.0)), compact ? Theme::hintSize() : Theme::labelSize(), contentAlpha, damage);

        if (workspace.empty && !compact)
            m_labels.render("Empty", workspace.rect.x + 18.0, workspace.rect.y + 44.0, std::max(1.0, workspace.rect.width - 36.0), Theme::footerSize(), contentAlpha * 0.42, damage);

        for (const auto& window : workspace.windows) {
            const auto windowSelected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
                m_selectedTarget,
                {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
            const auto windowBox   = boxFor(window.rect);
            const auto windowRound = Theme::windowRadius();
            const auto footerHeight = compact ? 0.0 : 28.0;

            drawRect(CBox{windowBox.x + Theme::shadowOffsetX(), windowBox.y + Theme::shadowOffsetY(), windowBox.w, windowBox.h},
                withAlpha(Theme::shadowColor(), contentAlpha), damage, windowRound + 4);

            drawRect(windowBox, Theme::windowFill(windowSelected, static_cast<float>(contentAlpha)), damage, windowRound);

            if (!compact && windowBox.h > 88.0) {
                const auto previewShell = CBox{
                    windowBox.x + 10.0,
                    windowBox.y + 10.0,
                    std::max(1.0, windowBox.w - 20.0),
                    std::max(1.0, windowBox.h - footerHeight - 16.0),
                };
                drawRect(previewShell, Theme::windowFill(windowSelected, static_cast<float>(contentAlpha)), damage, windowRound);
                renderWindowPreview(window, previewShell, contentAlpha, damage);

                drawRect(CBox{windowBox.x, windowBox.y + windowBox.h - footerHeight, windowBox.w, footerHeight},
                    Theme::windowFill(windowSelected, static_cast<float>(contentAlpha)), damage, windowRound);
                m_labels.render(window.label, window.rect.x + 14.0, window.rect.y + window.rect.height - footerHeight + 8.0,
                    std::max(1.0, window.rect.width - 28.0), Theme::footerSize(), contentAlpha, damage);
            } else {
                m_labels.render(window.label, window.rect.x + (compact ? 8.0 : 12.0), window.rect.y + (compact ? 5.0 : 8.0),
                    std::max(1.0, window.rect.width - (compact ? 16.0 : 20.0)), compact ? Theme::badgeSize() : Theme::footerSize(), contentAlpha, damage);
            }

            if (windowSelected) {
                const auto borderColor = Theme::windowBorder(true, static_cast<float>(contentAlpha));
                drawBorder(CBox{windowBox.x - 5.0, windowBox.y - 5.0, windowBox.w + 10.0, windowBox.h + 10.0}, borderColor, windowRound + 5, 3);
            }
        }
    }

    if (searchActive) {
        auto dim = Theme::backdropColor();
        dim.a = static_cast<float>(0.72 * alpha);
        drawRect(CBox{0.0, 0.0, frame.bounds.width, frame.bounds.height}, dim, damage);
        renderSearchPanel(frame, alpha, damage);
    }
}

void OverlayRenderer::renderHintDock(const WorkspaceWallFrame& frame, double contentAlpha, CHyprColor accent, CHyprColor railSurface,
    const CRegion& damage) {
const auto dockProgress = std::clamp(m_dockTransition.value(), 0.0, 1.0);
    if (!m_searchActive && dockProgress > 0.001) {
        // A shortcut dock: a small frosted capsule that rides up from the bottom edge, lit
        // along its rim by a two-stop gradient the way Hyprland lights a window border. It stays
        // hidden until the pointer reaches the bottom edge, so the stage is uncluttered by
        // shortcuts you already know.
        const auto dockAlpha = contentAlpha * dockProgress;
        const auto modeLabel = m_mode == OverviewMode::Grouped ? "APPS" : m_mode == OverviewMode::AppExpose ? "APP EXPOSÉ" : "SPATIAL";

        struct DeckHint {
            const char* keys;
            const char* action;
        };
        // Only bindings that actually exist: every letter key falls through to search, so there is
        // no hjkl to advertise.
        static constexpr std::array<DeckHint, 5> HINTS{{
                {"\xe2\x86\x90\xe2\x86\x92", "workspace"},
                {"\xe2\x86\x91\xe2\x86\x93", "window"},
                {"\xe2\x87\xa5", "group"},
                {"\xe2\x86\xb5", "open"},
                {"/", "find"},
            }};

        constexpr auto dockHeight   = 26.0;
        constexpr auto dockPadR     = 15.0;
        constexpr auto pillInset    = 4.0;
        constexpr auto pillPadX     = 11.0;
        constexpr auto pillGap      = 13.0;
        constexpr auto keysGap      = 6.0;
        constexpr auto pairGap      = 15.0;
        constexpr auto measureWidth = 240.0;
        constexpr auto riseDistance = 14.0;
        constexpr auto rimAngle     = 2.62F;

        const auto foreground = m_config.foregroundColor();
        // Derived from the live theme rather than a fixed pair, so the rim tracks accent_color and
        // falls back with it when the config leaves the defaults in place.
        const auto rimLit   = tintedSurface(accent, foreground, 0.42);
        const auto rimShade = withAlpha(accent, 0.16);

        const auto pillHeight = dockHeight - pillInset * 2.0;
        const auto modeSize   = m_labels.measure(modeLabel, measureWidth, Theme::hintSize(), foreground);
        const auto pillWidth  = modeSize.width + pillPadX * 2.0;

        auto contentWidth = pillInset + pillWidth + pillGap;
        std::array<double, HINTS.size()> keyWidths{};
        std::array<double, HINTS.size()> actionWidths{};
        for (std::size_t i = 0; i < HINTS.size(); ++i) {
            keyWidths[i]    = m_labels.measure(HINTS[i].keys, measureWidth, Theme::hintSize(), foreground).width;
            actionWidths[i] = m_labels.measure(HINTS[i].action, measureWidth, Theme::hintSize(), foreground).width;
            contentWidth += keyWidths[i] + keysGap + actionWidths[i] + (i + 1 < HINTS.size() ? pairGap : 0.0);
        }
        contentWidth += dockPadR;

        const auto dockWidth = std::min(std::max(1.0, frame.bounds.width - 64.0), contentWidth);
        // Rides up into place, so the reveal reads as the dock arriving rather than fading in.
        const auto dockY = frame.bounds.height - 34.0 - dockHeight + (1.0 - dockProgress) * riseDistance;
        const auto dock  = CBox{centered(frame.bounds.width, dockWidth), dockY, dockWidth, dockHeight};
        const auto radius = static_cast<int>(std::round(dockHeight / 2.0));

        // Ambient then contact shadow, matching how the window cards are lifted off the backdrop.
        drawRect(CBox{dock.x - 3.0, dock.y + 9.0, dock.w + 6.0, dock.h}, withAlpha(Theme::shadowColor(), dockAlpha * 0.40), damage, radius + 6);
        drawRect(CBox{dock.x + 3.0, dock.y + 5.0, dock.w - 6.0, dock.h}, withAlpha(Theme::shadowColor(), dockAlpha * 0.55), damage, radius);
        drawRect(dock, withAlpha(railSurface, dockAlpha * 0.90), damage, radius, true);

        drawBorder(dock, withAlpha(rimLit, dockAlpha * 0.55), rimShade, rimAngle, static_cast<float>(dockAlpha * 0.55), radius, 1);

        const auto pillBox = CBox{dock.x + pillInset, dock.y + pillInset, pillWidth, pillHeight};
        drawRect(pillBox, withAlpha(accent, dockAlpha * 0.95), damage, static_cast<int>(std::round(pillHeight / 2.0)), true);
        m_labels.renderCentered(modeLabel, pillBox, Theme::hintSize(), m_config.backgroundColor(), dockAlpha, damage);

        auto cursorX = pillBox.x + pillBox.w + pillGap;
        for (std::size_t i = 0; i < HINTS.size(); ++i) {
            const auto keySize    = m_labels.measure(HINTS[i].keys, measureWidth, Theme::hintSize(), foreground);
            const auto actionSize = m_labels.measure(HINTS[i].action, measureWidth, Theme::hintSize(), foreground);
            m_labels.renderColored(HINTS[i].keys, cursorX, dock.y + centered(dock.h, keySize.height), measureWidth,
                Theme::hintSize(), rimLit, dockAlpha * 0.96, damage);
            cursorX += keyWidths[i] + keysGap;
            m_labels.renderColored(HINTS[i].action, cursorX, dock.y + centered(dock.h, actionSize.height), measureWidth,
                Theme::hintSize(), foreground, dockAlpha * 0.58, damage);
            cursorX += actionWidths[i] + pairGap;
        }
    }
}

void OverlayRenderer::renderStageWindows(const WorkspaceWallFrame& frame, const StageContext& ctx, const CRegion& damage) {
    for (const auto& window : frame.stage.windows) {
        // A window that has already unmapped has nothing left to preview, and drawing its card
        // anyway left an empty surface sitting on the stage until the next collect landed.
        if (!findLiveWindow(window.stableId))
            continue;
        const auto selected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
            m_selectedTarget, {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
        const auto closeTransition = window.stableId == m_closingWindowId ? std::clamp(m_windowCloseTransition.value(), 0.0, 1.0) : 1.0;
        const auto cardAlpha       = ctx.stageAlpha * closeTransition;
        auto displayRect = remapStageRect(window.rect, frame.stage.bounds, ctx.pushedStageBounds);
        if (selected)
            displayRect = scaledAroundCenter(displayRect, std::lerp(0.995, 1.014, ctx.selectionTransition), -3.0 * ctx.selectionTransition);
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

        const auto windowAlpha = m_dragging && window.stableId == m_pointerDownTarget.windowId ? cardAlpha * 0.30 : cardAlpha;
        const auto lift = selected ? ctx.selectionTransition : 0.0;
        // Two shadow layers: a wide ambient one plus a tighter contact shadow, so cards float
        // instead of sitting flat on the backdrop.
        drawRect(CBox{windowBox.x - 4.0, windowBox.y + 6.0 + lift * 8.0, windowBox.w + 8.0, windowBox.h + 8.0},
            withAlpha(Theme::shadowColor(), windowAlpha * (0.34 + lift * 0.24)), damage, radius + 10);
        drawRect(CBox{windowBox.x + 5.0, windowBox.y + 9.0 + lift * 5.0, windowBox.w, windowBox.h},
            withAlpha(Theme::shadowColor(), windowAlpha * (0.62 + lift * 0.20)), damage, radius + 2);
        if (selected) {
            drawRect(CBox{windowBox.x - 8.0, windowBox.y - 8.0, windowBox.w + 16.0, windowBox.h + 16.0},
                withAlpha(ctx.accent, cardAlpha * 0.10 * ctx.selectionTransition), damage, radius + 8);
        }
        drawRect(windowBox, withAlpha(ctx.stageSurface, windowAlpha), damage, radius);
        renderWindowPreview(window, windowBox, windowAlpha, damage);
        // Glass top edge: a hairline highlight along the upper border, the glass card cue that
        // separates a floating surface from a flat rectangle.
        drawRect(CBox{windowBox.x + radius * 0.6, windowBox.y, std::max(0.0, windowBox.w - radius * 1.2), 1.0},
            surfaceColor(0.60F, windowAlpha * 0.20), damage);

        if (selected) {
            drawBorder(CBox{windowBox.x - 1.0, windowBox.y - 1.0, windowBox.w + 2.0, windowBox.h + 2.0}, withAlpha(ctx.accent, cardAlpha * 0.82),
                radius + 1, 1);

            const std::string state = window.fullscreen ? "Full" : window.floating ? "Float" : "";
            if (!state.empty() && windowBox.w >= 112.0 && windowBox.h >= 56.0) {
                const auto badge = CBox{windowBox.x + 10.0, windowBox.y + 10.0, 64.0, 22.0};
                drawRect(badge, withAlpha(ctx.railSurface, cardAlpha * 0.88), damage, 7, true);
                drawRect(CBox{badge.x + 8.0, badge.y + 10.0, 4.0, 4.0}, withAlpha(ctx.accent, cardAlpha), damage, 2);
                m_labels.render(state, badge.x + 18.0, badge.y + 5.0, 40.0, Theme::badgeSize(), cardAlpha * 0.92, damage);
            }
        }

        // Top-right corner, opposite the state badge so the two never collide. Drawn after the
        // preview so it sits over the thumbnail rather than under it.
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
                if (hotProgress > 0.001)
                    drawRect(CBox{closeBox.x - 5.0, closeBox.y - 5.0, closeBox.w + 10.0, closeBox.h + 10.0},
                        withAlpha(ctx.accent, reveal * 0.24 * hotProgress), damage, dot + 5);
                drawRect(CBox{closeBox.x + 1.0, closeBox.y + 2.0, closeBox.w, closeBox.h},
                    withAlpha(Theme::shadowColor(), reveal * 0.42), damage, dot);
                // Rests as a neutral surface and crossfades into the ctx.accent under the pointer.
                const auto fill = tintedSurface(surfaceColor(0.34F, 1.0), ctx.accent, hotProgress * 0.94);
                // Blur is rectangular before Hyprland applies the corner mask, which leaves a
                // grey square around this fully round control on hover.
                drawRect(closeBox, withAlpha(fill, reveal * 0.94), damage, dot);

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
        drawRect(CBox{titleBox.x + 3.0, titleBox.y + 4.0, titleBox.w, titleBox.h}, withAlpha(Theme::shadowColor(), cardAlpha * 0.52), damage, 9);
        drawRect(titleBox, withAlpha(titleSurface, cardAlpha * (selected ? 0.96 : 0.78)), damage, 9, true);
        if (selected)
            drawRect(CBox{titleBox.x + 12.0, titleBox.y + titleBox.h - 1.0, std::max(1.0, titleBox.w - 24.0), 1.0}, withAlpha(ctx.accent, cardAlpha * 0.64), damage, 1);
        m_labels.renderColored(appGlyph(window.appClass), titleBox.x + 11.0, titleBox.y + 6.0,
            18.0, Theme::hintSize(), ctx.accent, cardAlpha * (selected ? 1.0 : 0.82), damage);
        m_labels.render(window.label, titleBox.x + 33.0, titleBox.y + 6.0,
            std::max(1.0, titleBox.w - 45.0), Theme::hintSize(), cardAlpha * (selected ? 1.0 : 0.76), damage);
    }

    if (m_dragging) {
        const auto bounds = m_frameBoundsByMonitor.find(frame.monitorId);
        const auto* source = findWindowCard(m_pointerDownTarget.windowId);
        if (bounds != m_frameBoundsByMonitor.end() && source && contains(bounds->second, m_pointerPosition.x, m_pointerPosition.y)) {
            const auto local = mapGlobalPointToFrame(bounds->second, frame.bounds, m_pointerPosition.x, m_pointerPosition.y);
            const auto localX = local.x;
            const auto localY = local.y;
            const auto ghostWidth = std::clamp(source->rect.width * 0.58, 180.0, 320.0);
            const auto aspect = source->rect.height > 0.0 ? source->rect.width / source->rect.height : 16.0 / 9.0;
            const auto ghostHeight = std::clamp(ghostWidth / std::max(0.2, aspect), 100.0, 220.0);
            const auto ghost = CBox{localX - ghostWidth / 2.0, localY - ghostHeight / 2.0, ghostWidth, ghostHeight};
            drawRect(CBox{ghost.x + 9.0, ghost.y + 12.0, ghost.w, ghost.h}, withAlpha(Theme::shadowColor(), ctx.contentAlpha), damage, Theme::windowRadius() + 3);
            drawRect(ghost, surfaceColor(0.12F, ctx.contentAlpha * 0.96), damage, Theme::windowRadius());
            renderWindowPreview(*source, ghost, ctx.contentAlpha * 0.96, damage);
        }
    }
}

void OverlayRenderer::renderStageFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {

    const auto searchMultiplier = m_searchActive ? 0.16 : 1.0;
    // Monitors are not dimmed by which one holds the selection: every monitor shows its own
    // workspaces, so re-shading them on a pointer move reads as an unrelated screen flickering.
    const auto contentAlpha = alpha * searchMultiplier;
    const auto accent       = resolvedAccentColor();
    const auto railSurface  = surfaceColor(0.10F, 0.72);
    const auto stageSurface = surfaceColor(0.12F, 1.0);
    auto railBox            = boxFor(frame.rail.bounds);
    // A hover only changes the previewed workspace on the monitor under the pointer. Scoping the
    // stage transition to that monitor stops the others from replaying their entrance animation.
    const auto transition = m_stageTransitionMonitorId == -1 || m_stageTransitionMonitorId == frame.monitorId
        ? std::clamp(m_stageTransition.value(), 0.0, 1.0)
                            : 1.0;
    const auto shelfProgress = std::clamp(m_shelfTransition.value(), 0.0, 1.0);
    const auto displayedStageBounds = interpolatedRect(collapsedStageBounds(frame), frame.stage.bounds, shelfProgress);
    const auto selectionTransition = std::clamp(m_selectionTransition.value(), 0.0, 1.0);
    const auto railAlpha = contentAlpha * std::clamp(shelfProgress * 1.8, 0.0, 1.0);
    const auto railEntranceOffset = -(1.0 - alpha) * 18.0 - (1.0 - shelfProgress) * (railBox.y + railBox.h + 14.0);
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
        // Selection lifts the card, brightens it, and wraps it in an accent ring with a soft glow.
        if (selected)
            displayRect = scaledAroundCenter(displayRect, std::lerp(0.995, 1.032, selectionTransition), -5.0 * selectionTransition);
        const auto cardBox  = boxFor(displayRect);
        const auto radius   = Theme::workspaceRadius(true);

        if (selected && !workspace.createTarget) {
            drawRect(CBox{cardBox.x - 16.0, cardBox.y - 16.0, cardBox.w + 32.0, cardBox.h + 32.0},
                withAlpha(accent, railAlpha * 0.07 * selectionTransition), damage, radius + 16);
            drawRect(CBox{cardBox.x - 8.0, cardBox.y - 8.0, cardBox.w + 16.0, cardBox.h + 16.0},
                withAlpha(accent, railAlpha * 0.15 * selectionTransition), damage, radius + 8);
        }

        const auto lift = selected ? selectionTransition : 0.0;

        if (!workspace.createTarget) {
            drawRect(CBox{cardBox.x + 3.0, cardBox.y + 6.0 + lift * 5.0, cardBox.w, cardBox.h},
                withAlpha(Theme::shadowColor(), railAlpha * (0.44 + lift * 0.36)), damage, radius);
            // Selection carries roughly twice the lift of an idle card so the highlight is
            // legible at a glance rather than a few percent apart.
            const auto cardLift = workspace.empty ? 0.07F : selected ? 0.26F : 0.13F;
            const auto cardOpacity = workspace.empty ? 0.52 : selected ? 0.97 : 0.82;
            auto cardFill = surfaceColor(cardLift, railAlpha * cardOpacity);
            if (selected)
                cardFill = tintedSurface(cardFill, accent, 0.12);
            drawRect(cardBox, cardFill, damage, radius);
        }

        if (workspace.createTarget) {
            // A full-size outlined card rather than a small floating circle, so the create target
            // sits in the workspace row instead of orbiting beside it.
            drawRect(CBox{cardBox.x + 3.0, cardBox.y + 6.0 + lift * 5.0, cardBox.w, cardBox.h},
                withAlpha(Theme::shadowColor(), railAlpha * (0.22 + lift * 0.30)), damage, radius);
            // Needs a real surface, not just an accent wash: a translucent tint let the desktop
            // read straight through the card and made it look like a rendering artefact.
            auto createFill = surfaceColor(selected ? 0.20F : 0.11F, railAlpha * (selected ? 0.93 : 0.76));
            createFill = tintedSurface(createFill, accent, selected ? 0.18 : 0.10);
            drawRect(cardBox, createFill, damage, radius, true);
            const auto ringStrength = selected ? 0.88 : 0.46;
            drawBorder(cardBox, withAlpha(accent, railAlpha * ringStrength), radius, selected ? 2 : 1);
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

        if (!workspace.createTarget) {
            const auto borderStrength = selected ? 0.95 : workspace.active ? 0.30 : 0.10;
            drawBorder(cardBox, withAlpha(accent, railAlpha * borderStrength), radius, selected ? 2 : 1);
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

    // Depth push: the outgoing workspace recedes and fades out while the incoming one settles
    // forward from a slightly larger scale. No lateral travel, so the change reads as depth.
    const auto stageAlpha        = contentAlpha * transition;
    const auto pushedStageBounds = scaledAroundCenter(displayedStageBounds, std::lerp(1.08, 1.0, transition));

    if (previousFrame && previousFrame->stage.workspaceId != frame.stage.workspaceId && transition < 1.0) {
        const auto previousAlpha = contentAlpha * (1.0 - transition);
        const auto previousScale = std::lerp(1.0, 0.92, transition);
        for (const auto& window : previousFrame->stage.windows) {
            const auto previousDisplayedStage = scaledAroundCenter(
                interpolatedRect(collapsedStageBounds(*previousFrame), previousFrame->stage.bounds, shelfProgress), previousScale);
            const auto previousBox = boxFor(remapStageRect(window.rect, previousFrame->stage.bounds, previousDisplayedStage));
            const auto radius = Theme::windowRadius();
            drawRect(CBox{previousBox.x + 7.0, previousBox.y + 10.0, previousBox.w, previousBox.h},
                withAlpha(Theme::shadowColor(), previousAlpha * 0.72), damage, radius + 2);
            drawRect(previousBox, surfaceColor(0.12F, previousAlpha), damage, radius);
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
        .selectionTransition  = selectionTransition,
        .accent               = accent,
        .stageSurface         = stageSurface,
        .railSurface          = railSurface,
        .displayedStageBounds = displayedStageBounds,
        .pushedStageBounds    = pushedStageBounds,
    };
    renderStageWindows(frame, stageCtx, damage);

    renderHintDock(frame, contentAlpha, accent, railSurface, damage);

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

    const auto sourceSize = texture->m_size == Vector2D{} ? window->m_realSize->value() : texture->m_size;
    const auto targetBox  = fillBoxForAspect(insetBox(clipBox, 2.0), sourceSize.x, sourceSize.y);
    if (targetBox.w <= 0.0 || targetBox.h <= 0.0)
        return;

    CTexPassElement::SRenderData data;
    data.tex      = texture;
    data.box      = targetBox;
    data.a        = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    data.overallA = data.a;
    data.damage   = damage;
    data.round    = Theme::windowRadius();
    data.clipBox  = clipBox;
    data.surface  = surface;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
}

void OverlayRenderer::renderSearchPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    if (!m_searchActive)
        return;

    const auto targets        = matchingSearchTargets();
    const auto geometry       = computeSearchPanelGeometry(frame, targets.size());
    const auto visibleStart   = visibleSearchStart(targets, m_selectedTarget, geometry.capacity);
    const auto visibleEnd     = std::min(targets.size(), visibleStart + geometry.capacity);

    const auto accent = resolvedAccentColor();
    const auto background = m_config.backgroundColor();

    const auto panelBox = CBox{geometry.panelX, geometry.panelY, geometry.panelW, geometry.panelH};
    const auto inputBox = CBox{geometry.inputX, geometry.inputY, geometry.inputW, geometry.inputH};

    drawRect(CBox{panelBox.x + Theme::shadowOffsetX(), panelBox.y + Theme::shadowOffsetY(), panelBox.w, panelBox.h},
        withAlpha(Theme::shadowColor(), alpha), damage, Theme::searchRadius());
    drawRect(panelBox, withAlpha(tintedSurface(Theme::searchPanelColor(), background, 0.34), alpha), damage, Theme::searchRadius(), true);

    drawBorder(panelBox, withAlpha(accent, alpha * 0.62), Theme::searchRadius(), 1);

    drawRect(inputBox, withAlpha(tintedSurface(Theme::searchInputColor(), background, 0.28), alpha), damage, Theme::inputRadius());

    // Measure through the shared cache rather than a second hand-rolled lookup with its own key
    // format: the old IIFE inserted a differently-keyed entry for the same ">" the renderLabel below
    // already caches.
    const auto promptSize   = m_labels.measure(">", 32.0, Theme::labelSize(), m_config.foregroundColor());
    const auto promptWidth  = promptSize.width;
    const auto promptHeight = promptSize.height;
    const auto textY        = inputBox.y + std::round((inputBox.h - promptHeight) / 2.0);
    const auto promptX      = inputBox.x + 16.0;
    const auto queryX       = promptX + promptWidth + 10.0;

    m_labels.render(">", promptX, textY, 32.0, Theme::labelSize(), alpha, damage);
    m_labels.render(std::format("{}_", m_searchQuery), queryX, textY,
        std::max(1.0, inputBox.x + inputBox.w - 92.0 - queryX), Theme::labelSize(), alpha, damage);
    m_labels.render(std::format("{} result{}", targets.size(), targets.size() == 1 ? "" : "s"),
        inputBox.x + inputBox.w - 84.0, inputBox.y + 17.0, 72.0, Theme::hintSize(), alpha * 0.50, damage);

    for (std::size_t index = visibleStart; index < visibleEnd; ++index) {
        const auto& target  = targets[index];
        const auto selected = sameTarget(target, m_selectedTarget);
        const auto rowIndex = index - visibleStart;
        const auto row      = CBox{
            inputBox.x,
            geometry.resultsY + static_cast<double>(rowIndex) * (geometry.rowHeight + geometry.rowGap),
            inputBox.w,
            geometry.rowHeight,
        };

        drawRect(row, selected ? withAlpha(accent, alpha * 0.18) : Theme::searchRowFill(false, static_cast<float>(alpha)), damage, Theme::inputRadius());
        if (selected) {
            const auto accentHeight = std::max(1.0, row.h - 20.0);
            drawRect(CBox{row.x, row.y + (row.h - accentHeight) / 2.0, 4.0, accentHeight},
                withAlpha(accent, alpha), damage, 2);
        }

        const auto badgeBox = CBox{row.x + 18.0, row.y + 16.0, 72.0, 24.0};
        if (target.type == OverviewTargetType::Workspace) {
            const auto* workspace = findWorkspaceCard(target.workspaceId);
            const auto  name      = workspace && !workspace->name.empty() ? workspace->name : std::to_string(target.workspaceId);
            drawRect(badgeBox, withAlpha(Theme::searchBadgeSpace(), alpha), damage, Theme::inputRadius());
            m_labels.render("SPACE", row.x + 31.0, row.y + 23.0, 46.0, Theme::badgeSize(), alpha * 0.78, damage);
            m_labels.render(std::format("Workspace {}", name), row.x + 104.0, row.y + 8.0, row.w - 128.0, Theme::labelSize(), alpha, damage);
            m_labels.render("Switch to this workspace", row.x + 104.0, row.y + 30.0, row.w - 128.0, Theme::hintSize(), alpha * 0.50, damage);
        } else if (const auto* window = findWindowCard(target.windowId)) {
            drawRect(badgeBox, withAlpha(Theme::searchBadgeWindow(), alpha), damage, Theme::inputRadius());
            m_labels.render("WINDOW", row.x + 26.0, row.y + 23.0, 56.0, Theme::badgeSize(), alpha * 0.78, damage);
            m_labels.render(window->label, row.x + 104.0, row.y + 8.0, row.w - 128.0, Theme::labelSize(), alpha, damage);
            m_labels.render(std::format("Workspace {}", window->workspaceId), row.x + 104.0, row.y + 30.0, row.w - 128.0, Theme::hintSize(), alpha * 0.50, damage);
        }
    }

    if (targets.empty()) {
        m_labels.render("No results", inputBox.x + 4.0, geometry.resultsY + 18.0, inputBox.w - 8.0, Theme::titleSize(), alpha * 0.78, damage);
        m_labels.render("Try another window title, workspace name, or number", inputBox.x + 4.0, geometry.resultsY + 48.0,
            inputBox.w - 8.0, Theme::hintSize(), alpha * 0.48, damage);
    }

    m_labels.render("\xe2\x86\x91\xe2\x86\x93 select \xc2\xb7 Enter activate \xc2\xb7 Esc clear",
        panelBox.x + 28.0, panelBox.y + panelBox.h - 24.0,
        panelBox.w - 56.0, Theme::hintSize(), alpha * 0.48, damage);
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
        if (const auto monitor = g_pCompositor->getMonitorFromCursor()) {
            if (const auto* frame = frameForMonitor(monitor->m_id))
                return frame;
        }
    }

    if (m_frames.empty())
        return nullptr;

    return &m_frames.front();
}

CHyprColor OverlayRenderer::resolvedAccentColor() const {
    if (const auto configured = m_config.accentColorOverride())
        return *configured;

    if (const auto focusedWindow = Desktop::focusState() ? Desktop::focusState()->window() : nullptr) {
        if (!focusedWindow->m_realBorderColor.m_colors.empty()) {
            auto accent = focusedWindow->m_realBorderColor.m_colors.front();
            accent.a = 1.0;
            return accent;
        }
    }

    // Omarchy drives col.active_border from the same theme accent, so this fallback agrees with
    // the border on Omarchy and still resolves sensibly elsewhere.
    const auto& accent = m_config.palette().accent;
    return {accent.red, accent.green, accent.blue, accent.alpha};
}

CHyprColor OverlayRenderer::surfaceColor(float lift, double alpha) const {
    const auto& palette = m_config.palette();
    const auto  lifted  = liftedSurface(palette, palette.background, lift);
    return {lifted.red, lifted.green, lifted.blue, static_cast<float>(std::clamp(alpha, 0.0, 1.0))};
}

void OverlayRenderer::damageMonitorById(std::int64_t monitorId) const {
    if (!g_pCompositor || !g_pHyprRenderer)
        return;

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor || monitor->m_id != monitorId || !g_pCompositor->monitorExists(monitor))
            continue;

        g_pHyprRenderer->damageMonitor(monitor);
        g_pCompositor->scheduleFrameForMonitor(monitor);
        return;
    }
}

void OverlayRenderer::damageAllMonitors() const {
    if (!g_pCompositor || !g_pHyprRenderer)
        return;

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor || !g_pCompositor->monitorExists(monitor))
            continue;

        g_pHyprRenderer->damageMonitor(monitor);
        g_pCompositor->scheduleFrameForMonitor(monitor);
    }
}

} // namespace hypr_radiant
