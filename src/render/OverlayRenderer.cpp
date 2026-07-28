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
constexpr auto WORKSPACE_PUSH_SCALE = 1.3;
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

OverlayRenderer::OverlayRenderer(const RadiantConfig& config, PreferencesStore& preferences) :
    m_config(config), m_preferences(preferences), m_labels(config) {}

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
    m_preferencesVisible = false;
    m_preferencesMonitorId = -1;
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
    m_animation.animateTo(true, effectiveAnimationDurationMs());
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
        static constexpr std::array controls{
            PreferenceControl::WorkspaceView,
            PreferenceControl::WindowView,
            PreferenceControl::Accent,
            PreferenceControl::AppExpose,
        };
        const auto current = std::ranges::find(controls, m_selectedPreference);
        auto index = current == controls.end() ? std::size_t{0} : static_cast<std::size_t>(std::distance(controls.begin(), current));
        if (direction == NavigationDirection::Up)
            index = index == 0 ? controls.size() - 1 : index - 1;
        else if (direction == NavigationDirection::Down)
            index = (index + 1) % controls.size();
        else if (m_selectedPreference != PreferenceControl::AppExpose)
            (void)applyPreference(m_selectedPreference);
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
    m_selectedTarget = m_hitTester.moveSelection(*frame, m_selectedTarget, direction);
    m_selectedFrameMonitorId = frameMonitorId;
    if (!sameTarget(previousTarget, m_selectedTarget))
        animateSelection();
    if (effectiveLayoutMode() == LayoutMode::Stage && m_selectedTarget.workspaceId != previousWorkspace) {
        m_previousFrames = m_frames;
        rebuildFrames();
        m_stageTransitionMonitorId = frameMonitorId;
        m_stageTransition.hideImmediate();
        // Longer than the open animation: the depth push needs room to read as movement.
        m_stageTransition.animateTo(true, std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * WORKSPACE_PUSH_SCALE))));
    }
    damageMonitorById(frameMonitorId);
}

void OverlayRenderer::selectTargetAt(double x, double y) {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return;

    if (!m_searchActive && effectiveLayoutMode() == LayoutMode::Stage) {
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
    if (!m_searchActive && effectiveLayoutMode() == LayoutMode::Stage && target.workspaceId != previousWorkspace) {
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
            m_stageTransition.animateTo(true, std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * WORKSPACE_PUSH_SCALE))));
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
        if (m_preferencesVisible) {
            m_pointerDownPreference = preferenceControlAt(x, y);
            return {};
        }
        m_pointerDownTarget = hitTest(x, y);
        m_dragTarget = {};
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
    }
    damageAllMonitors();
}

PointerAction OverlayRenderer::activatePreference() {
    if (!m_preferencesVisible)
        return {};
    return applyPreference(m_selectedPreference);
}

void OverlayRenderer::setWorkspaceShelfVisible(bool visible) {
    if (effectiveLayoutMode() != LayoutMode::Stage || !active() || m_shelfTransition.targetVisible() == visible)
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
    setWorkspaceShelfVisible(!m_shelfTransition.targetVisible());
}

void OverlayRenderer::setWorkspaceShelfGestureProgress(bool revealing, double progress) {
    if (effectiveLayoutMode() != LayoutMode::Stage || !active())
        return;

    m_shelfTransition.setProgress(revealing ? progress : 1.0 - progress, revealing);
    damageAllMonitors();
}

void OverlayRenderer::finishWorkspaceShelfGesture(bool revealing, bool commit) {
    const auto visible = revealing ? commit : !commit;
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
}

void OverlayRenderer::animateSelection() {
    m_selectionTransition.hideImmediate();
    m_selectionTransition.animateTo(true, std::max(0, static_cast<int>(std::round(effectiveAnimationDurationMs() * 0.62))));
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

OverviewTarget OverlayRenderer::hitTest(double x, double y) const {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return {};

    if (!m_searchActive && effectiveLayoutMode() == LayoutMode::Stage) {
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
    if (effectiveLayoutMode() == LayoutMode::WorkspaceWall)
        backdrop = tintedSurface(backdrop, resolvedAccentColor(), 0.08);
    backdrop.a *= backdropAlpha;
    drawRect(box, backdrop, damage, 0, true);

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
                layoutOptionsFor(effectiveLayoutMode(), previewWorkspaceId, m_mode, m_applicationFilter)));
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
                layoutOptionsFor(effectiveLayoutMode(), previewWorkspaceId, m_mode, m_applicationFilter)));
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
    const auto accent       = resolvedAccentColor();
    const auto foreground   = m_config.foregroundColor();
    const auto accentLit    = tintedSurface(accent, foreground, 0.24);
    const auto entrance     = std::clamp(m_stageTransition.value(), 0.0, 1.0);
    const auto selection    = easedProgress(m_selectionTransition.value());
    // Typography follows the actual interactive progress, not the animation's eventual target.
    // Gesture-driven closes keep targetVisible() true until release, so a target-based curve left
    // labels fully present and then dropped their textures at the end. This delayed smoothstep
    // makes them fade continuously after the glass on reveal and before it on dismissal.
    const auto typographyFade = easedProgress((alpha - 0.08) / 0.72);
    const auto headingAlpha = contentAlpha * typographyFade;

    double contentLeft   = frame.bounds.width;
    double contentTop    = frame.bounds.height;
    bool   hasContent    = false;
    for (const auto& workspace : frame.workspaces) {
        if (workspace.rect.width <= 0.0 || workspace.rect.height <= 0.0)
            continue;
        contentLeft   = std::min(contentLeft, workspace.rect.x);
        contentTop    = std::min(contentTop, workspace.rect.y);
        hasContent    = true;
    }

    const auto titleX = hasContent ? contentLeft : 46.0;
    const auto titleY = hasContent ? std::max(30.0, contentTop - 58.0) : 30.0;

    const auto headingY = titleY + (1.0 - entrance) * 10.0;
    m_labels.renderColored("WORKSPACES", titleX, headingY, std::max(1.0, frame.bounds.width * 0.45),
        Theme::titleSize(), accentLit, headingAlpha, damage);

    for (std::size_t workspaceIndex = 0; workspaceIndex < frame.workspaces.size(); ++workspaceIndex) {
        const auto& workspace = frame.workspaces[workspaceIndex];
        const auto ownsSelection = frame.monitorId == m_selectedFrameMonitorId &&
            m_selectedTarget.workspaceId == workspace.workspaceId;
        const auto workspaceSelected = ownsSelection &&
            sameTarget(m_selectedTarget, {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId});
        const auto compact      = workspace.rect.height <= 120.0;
        const auto round        = compact ? 14 : 18;
        const auto stagger      = frame.workspaces.size() <= 1 ? 0.0 :
            static_cast<double>(workspaceIndex) / static_cast<double>(frame.workspaces.size() - 1) * 0.13;
        const auto cardEntrance = easedProgress((entrance - stagger) / std::max(0.01, 1.0 - stagger));
        const auto hoverLift    = ownsSelection ? selection : 0.0;
        auto       displayRect  = scaledAroundCenter(workspace.rect,
            std::lerp(0.94, 1.0, cardEntrance) * std::lerp(1.0, workspaceSelected ? 1.018 : 1.006, hoverLift),
            -std::lerp(0.0, workspaceSelected ? 7.0 : 3.0, hoverLift));
        displayRect.y += (1.0 - cardEntrance) * (28.0 + static_cast<double>(workspaceIndex % 3) * 7.0);
        const auto workspaceBox = boxFor(displayRect);
        const auto cardAlpha    = contentAlpha * cardEntrance;
        const auto detailAlpha  = cardAlpha * typographyFade;

        if ((ownsSelection || workspace.active) && !compact) {
            const auto glowStrength = workspaceSelected ? 0.075 * selection : workspace.active ? 0.032 : 0.02 * selection;
            const auto spread       = workspaceSelected ? 11.0 : 7.0;
            const auto glowBox = CBox{
                workspaceBox.x - spread,
                workspaceBox.y - spread,
                workspaceBox.w + spread * 2.0,
                workspaceBox.h + spread * 2.0,
            };
            drawRect(glowBox, withAlpha(accent, cardAlpha * glowStrength), damage, round + static_cast<int>(spread));
        }

        const auto shadowLift = workspaceSelected ? 9.0 * selection : ownsSelection ? 4.0 * selection : 0.0;
        drawRect(CBox{workspaceBox.x + 5.0, workspaceBox.y + 7.0 + shadowLift * 0.30, workspaceBox.w, workspaceBox.h},
            withAlpha(Theme::shadowColor(), cardAlpha * (0.34 + hoverLift * 0.12)), damage, round + 2);

        const auto surfaceLift = workspace.empty ? 0.085F :
            workspaceSelected ? static_cast<float>(0.145 + selection * 0.035) : workspace.active ? 0.13F : ownsSelection ? 0.125F : 0.105F;
        auto cardSurface = surfaceColor(surfaceLift, cardAlpha * (workspace.empty ? 0.58 : 0.70));
        cardSurface = tintedSurface(cardSurface, accent,
            workspaceSelected ? 0.14 + selection * 0.08 : workspace.active ? 0.11 : ownsSelection ? 0.10 : 0.045);
        drawRect(workspaceBox, cardSurface, damage, round, true);

        // Hairlines establish the card edge; state is carried by a short coloured rail instead of
        // a thick neon frame. Window hover intentionally does not outline its whole workspace.
        drawBorder(workspaceBox, withAlpha(foreground, cardAlpha * 0.065), round, 1);
        if (workspace.active) {
            const auto railWidth = std::min(48.0, workspaceBox.w * 0.18);
            drawRect(CBox{workspaceBox.x + 16.0, workspaceBox.y, railWidth, 1.0},
                withAlpha(accent, cardAlpha * 0.58), damage, 1);
        }
        if (workspaceSelected) {
            const auto cornerAlpha = cardAlpha * std::lerp(0.24, 0.76, selection);
            constexpr auto cornerLength = 20.0;
            constexpr auto cornerInset  = 7.0;
            constexpr auto cornerWeight = 2.0;
            const auto left   = workspaceBox.x + cornerInset;
            const auto right  = workspaceBox.x + workspaceBox.w - cornerInset;
            const auto top    = workspaceBox.y + cornerInset;
            const auto bottom = workspaceBox.y + workspaceBox.h - cornerInset;
            drawRect(CBox{left, top, cornerLength, cornerWeight}, withAlpha(accentLit, cornerAlpha), damage, 1);
            drawRect(CBox{left, top, cornerWeight, cornerLength}, withAlpha(accentLit, cornerAlpha), damage, 1);
            drawRect(CBox{right - cornerLength, top, cornerLength, cornerWeight}, withAlpha(accentLit, cornerAlpha), damage, 1);
            drawRect(CBox{right - cornerWeight, top, cornerWeight, cornerLength}, withAlpha(accentLit, cornerAlpha), damage, 1);
            drawRect(CBox{left, bottom - cornerWeight, cornerLength, cornerWeight}, withAlpha(accentLit, cornerAlpha), damage, 1);
            drawRect(CBox{left, bottom - cornerLength, cornerWeight, cornerLength}, withAlpha(accentLit, cornerAlpha), damage, 1);
            drawRect(CBox{right - cornerLength, bottom - cornerWeight, cornerLength, cornerWeight}, withAlpha(accentLit, cornerAlpha), damage, 1);
            drawRect(CBox{right - cornerWeight, bottom - cornerLength, cornerWeight, cornerLength}, withAlpha(accentLit, cornerAlpha), damage, 1);
        }

        const auto headerHeight = compact ? 28.0 : std::clamp(workspaceBox.h * 0.12, 34.0, 42.0);
        drawRect(CBox{workspaceBox.x + 14.0, workspaceBox.y + headerHeight, std::max(0.0, workspaceBox.w - 28.0), 1.0},
            withAlpha(ownsSelection ? accent : foreground, cardAlpha * (ownsSelection ? 0.12 : 0.055)), damage);
        const auto workspaceCode = std::format("{:02}", workspace.workspaceId);
        const auto workspaceLabel = workspace.name.empty() || workspace.name == std::to_string(workspace.workspaceId) ?
            workspaceCode : workspace.name;
        m_labels.renderColored(workspaceLabel, workspaceBox.x + (compact ? 10.0 : 15.0),
            workspaceBox.y + (compact ? 7.0 : 9.0), std::max(1.0, workspaceBox.w - (compact ? 20.0 : 30.0)),
            compact ? Theme::hintSize() : Theme::labelSize(), ownsSelection ? accentLit : foreground,
            detailAlpha * (ownsSelection ? 0.96 : 0.72), damage);

        for (const auto& window : workspace.windows) {
            const auto windowSelected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
                m_selectedTarget,
                {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
            auto windowRect = remapRect(window.rect, workspace.rect, displayRect);
            if (windowSelected)
                windowRect = scaledAroundCenter(windowRect, std::lerp(1.0, 1.018, selection), -3.0 * selection);
            const auto windowBox   = boxFor(windowRect);
            const auto windowRound = Theme::windowRadius();
            const auto footerHeight = compact ? 0.0 : 28.0;

            if (windowSelected)
                drawRect(CBox{windowBox.x - 6.0, windowBox.y - 6.0, windowBox.w + 12.0, windowBox.h + 12.0},
                    withAlpha(accent, cardAlpha * 0.055 * selection), damage, windowRound + 6);
            drawRect(CBox{windowBox.x + 3.0, windowBox.y + 5.0 + selection * (windowSelected ? 2.0 : 0.0), windowBox.w, windowBox.h},
                withAlpha(Theme::shadowColor(), cardAlpha * (windowSelected ? 0.52 : 0.34)), damage, windowRound + 2);

            auto windowSurface = surfaceColor(windowSelected ? 0.25F : 0.19F, cardAlpha * 0.82);
            windowSurface = tintedSurface(windowSurface, accent, windowSelected ? 0.18 * selection : 0.055);
            drawRect(windowBox, windowSurface, damage, windowRound);

            if (!compact && windowBox.h > 88.0) {
                const auto previewShell = CBox{
                    windowBox.x + 7.0,
                    windowBox.y + 7.0,
                    std::max(1.0, windowBox.w - 14.0),
                    std::max(1.0, windowBox.h - footerHeight - 10.0),
                };
                drawRect(previewShell, surfaceColor(0.08F, cardAlpha * 0.92), damage, windowRound - 2);
                renderWindowPreview(window, previewShell, cardAlpha, damage);

                m_labels.renderColored(appGlyph(window.appClass), windowBox.x + 11.0, windowBox.y + windowBox.h - footerHeight + 8.0,
                    18.0, Theme::hintSize(), accent, detailAlpha * (windowSelected ? 1.0 : 0.70), damage);
                m_labels.render(window.label, windowBox.x + 32.0, windowBox.y + windowBox.h - footerHeight + 7.0,
                    std::max(1.0, windowBox.w - 43.0), Theme::footerSize(), detailAlpha * (windowSelected ? 1.0 : 0.78), damage);
            } else {
                m_labels.render(window.label, windowBox.x + (compact ? 8.0 : 12.0), windowBox.y + (compact ? 5.0 : 8.0),
                    std::max(1.0, windowBox.w - (compact ? 16.0 : 20.0)), compact ? Theme::badgeSize() : Theme::footerSize(),
                    detailAlpha * (windowSelected ? 1.0 : 0.76), damage);
            }

            if (windowSelected) {
                drawBorder(windowBox, withAlpha(accentLit, cardAlpha * std::lerp(0.22, 0.66, selection)),
                    windowRound, 1);
                drawRect(CBox{windowBox.x + 12.0, windowBox.y, std::min(72.0, windowBox.w * 0.34), 1.0},
                    withAlpha(accentLit, cardAlpha * 0.78 * selection), damage, 1);
            }
        }
    }

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
        static constexpr std::array<DeckHint, 6> STAGE_HINTS{{
                {"\xe2\x86\x90\xe2\x86\x92", "workspace"},
                {"\xe2\x86\x91\xe2\x86\x93", "window"},
                {"tab", "apps/spatial"},
                {"\xe2\x86\xb5", "open"},
                {"/", "find"},
                {"ctrl+,", "settings"},
            }};
        static constexpr std::array<DeckHint, 5> WALL_HINTS{{
                {"\xe2\x86\x90\xe2\x86\x92", "workspace"},
                {"\xe2\x86\x91\xe2\x86\x93", "window"},
                {"\xe2\x86\xb5", "open"},
                {"/", "find"},
                {"ctrl+,", "settings"},
            }};
        const auto hints = effectiveLayoutMode() == LayoutMode::WorkspaceWall ?
            std::span<const DeckHint>{WALL_HINTS} : std::span<const DeckHint>{STAGE_HINTS};

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
        std::array<double, STAGE_HINTS.size()> keyWidths{};
        std::array<double, STAGE_HINTS.size()> actionWidths{};
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

        drawRect(CBox{dock.x - 3.0, dock.y + 9.0, dock.w + 6.0, dock.h},
            withAlpha(Theme::shadowColor(), dockAlpha * 0.40), damage, radius + 6);
        drawRect(CBox{dock.x + 3.0, dock.y + 5.0, dock.w - 6.0, dock.h},
            withAlpha(Theme::shadowColor(), dockAlpha * 0.55), damage, radius);
        drawRect(dock, withAlpha(railSurface, dockAlpha * 0.90), damage, radius, true);
        drawBorder(dock, withAlpha(rimLit, dockAlpha * 0.55), rimShade, rimAngle,
            static_cast<float>(dockAlpha * 0.55), radius, 1);

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

    const auto geometry   = computePreferencesPanel(frame.bounds);
    const auto accent     = resolvedAccentColor();
    const auto foreground = m_config.foregroundColor();
    const auto panelBox   = boxFor(geometry.panel);
    const auto panelAlpha = std::clamp(alpha, 0.0, 1.0);

    auto dim = m_config.backgroundColor();
    dim.a = static_cast<float>(0.68 * panelAlpha);
    drawRect(CBox{0.0, 0.0, frame.bounds.width, frame.bounds.height}, dim, damage);
    drawRect(CBox{panelBox.x + 8.0, panelBox.y + 10.0, panelBox.w, panelBox.h},
        withAlpha(Theme::shadowColor(), panelAlpha * 0.74), damage);
    drawRect(panelBox, withAlpha(m_config.backgroundColor(), panelAlpha * 0.95), damage, 0, true);
    drawBorder(panelBox, withAlpha(foreground, panelAlpha * 0.82), 0, 2);

    drawRect(CBox{panelBox.x, panelBox.y, 4.0, 58.0}, withAlpha(accent, panelAlpha), damage);
    drawRect(CBox{panelBox.x + 1.0, panelBox.y + 57.0, panelBox.w - 2.0, 1.0},
        withAlpha(foreground, panelAlpha * 0.16), damage);
    m_labels.renderColored("> radiant preferences", panelBox.x + 22.0, panelBox.y + 17.0,
        panelBox.w - 150.0, Theme::footerSize(), accent, panelAlpha, damage);
    m_labels.renderColored("~/.config/hypr-radiant/preferences.conf", panelBox.x + 22.0, panelBox.y + 39.0,
        panelBox.w - 170.0, Theme::badgeSize(), foreground, panelAlpha * 0.42, damage);

    const auto closeBox = boxFor(geometry.closeButton);
    const auto closeSelected = m_selectedPreference == PreferenceControl::Close;
    drawRect(closeBox, withAlpha(foreground, panelAlpha * (closeSelected ? 0.12 : 0.05)), damage);
    drawBorder(closeBox, withAlpha(foreground, panelAlpha * 0.20), 0, 1);
    m_labels.renderCentered("X", closeBox, Theme::hintSize(),
        closeSelected ? accent : foreground, panelAlpha * (closeSelected ? 1.0 : 0.58), damage);

    m_labels.renderColored("OVERVIEW", geometry.panel.x + 30.0, geometry.rows[0].rect.y - 23.0,
        geometry.panel.width - 60.0, Theme::badgeSize(), accent, panelAlpha * 0.72, damage);
    drawRect(CBox{geometry.panel.x + 98.0, geometry.rows[0].rect.y - 18.0, geometry.panel.width - 128.0, 1.0},
        withAlpha(foreground, panelAlpha * 0.11), damage);
    m_labels.renderColored("INTERFACE", geometry.panel.x + 30.0, geometry.rows[2].rect.y - 23.0,
        geometry.panel.width - 60.0, Theme::badgeSize(), accent, panelAlpha * 0.72, damage);
    drawRect(CBox{geometry.panel.x + 106.0, geometry.rows[2].rect.y - 18.0, geometry.panel.width - 136.0, 1.0},
        withAlpha(foreground, panelAlpha * 0.11), damage);

    const std::array rowLabels{
        "01  workspace.layout",
        "02  windows.arrangement",
        "03  interface.accent",
    };

    for (std::size_t i = 0; i < geometry.rows.size(); ++i) {
        const auto& row = geometry.rows[i];
        const auto selected = row.control == m_selectedPreference;
        const auto rowBox = boxFor(row.rect);
        drawRect(rowBox, withAlpha(foreground, panelAlpha * (selected ? 0.07 : 0.025)), damage);
        drawRect(CBox{rowBox.x, rowBox.y + 8.0, 3.0, rowBox.h - 16.0},
            withAlpha(selected ? accent : foreground, panelAlpha * (selected ? 0.92 : 0.10)), damage);

        m_labels.renderColored(rowLabels[i], row.rect.x + 15.0, row.rect.y + 17.0,
            std::max(1.0, row.rect.width * 0.40), Theme::hintSize(), selected ? accent : foreground,
            panelAlpha * (selected ? 0.92 : 0.52), damage);
    }

    const auto activeOption = [this](PreferenceControl control) {
        switch (control) {
        case PreferenceControl::WorkspaceView:
            return effectiveLayoutMode() == LayoutMode::WorkspaceWall ? 1 : 0;
            case PreferenceControl::WindowView: return m_preferences.state().windowView == WindowViewPreference::Grouped ? 1 : 0;
            case PreferenceControl::Accent: return static_cast<int>(m_preferences.state().accent);
        case PreferenceControl::None:
        case PreferenceControl::AppExpose:
        case PreferenceControl::Close:
            return -1;
        }
        return -1;
    };
    const auto optionLabel = [](PreferenceControl control, int value) -> std::string {
        switch (control) {
        case PreferenceControl::WorkspaceView:
            return value == 0 ? "STAGE" : "WALL";
        case PreferenceControl::WindowView:
            return value == 0 ? "SPATIAL" : "GROUPED";
        case PreferenceControl::Accent: {
                static constexpr std::array labels{"THEME", "GREEN", "BLUE", "VIOLET"};
                return labels[static_cast<std::size_t>(std::clamp(value, 0, 3))];
        }
        case PreferenceControl::None:
        case PreferenceControl::AppExpose:
        case PreferenceControl::Close:
            return {};
        }
        return {};
    };
    for (const auto& option : geometry.options) {
        const auto optionBox = boxFor(option.rect);
        const auto active = option.value == activeOption(option.control);
        drawRect(optionBox, withAlpha(foreground, panelAlpha * (active ? 0.10 : 0.025)), damage);
        drawBorder(optionBox, withAlpha(active ? accent : foreground, panelAlpha * (active ? 0.58 : 0.11)), 0, 1);
        m_labels.renderCentered(optionLabel(option.control, option.value), optionBox, Theme::badgeSize(),
            active ? accent : foreground, panelAlpha * (active ? 1.0 : 0.48), damage);
    }

    const auto appSelected = m_selectedPreference == PreferenceControl::AppExpose;
    const auto appBox = boxFor(geometry.appExposeButton);
    drawRect(appBox, withAlpha(foreground, panelAlpha * (appSelected ? 0.09 : 0.035)), damage);
    drawBorder(appBox, withAlpha(appSelected ? accent : foreground, panelAlpha * (appSelected ? 0.72 : 0.14)), 0, 1);
    m_labels.renderColored("> radiant app --focused", geometry.appExposeButton.x + 16.0, geometry.appExposeButton.y + 11.0,
        geometry.appExposeButton.width - 128.0, Theme::hintSize(),
        appSelected ? accent : foreground, panelAlpha * 0.72, damage);
    const auto runBox = CBox{
        geometry.appExposeButton.x + geometry.appExposeButton.width - 96.0,
        geometry.appExposeButton.y + 5.0,
        84.0,
        geometry.appExposeButton.height - 10.0,
    };
    drawRect(runBox, withAlpha(foreground, panelAlpha * 0.07), damage);
    drawBorder(runBox, withAlpha(accent, panelAlpha * 0.48), 0, 1);
    m_labels.renderCentered("[run ↵]", runBox, Theme::badgeSize(), accent, panelAlpha, damage);

    m_labels.renderColored("↑↓ navigate   ←→ change   enter apply   ctrl+, close",
        geometry.panel.x + 28.0, geometry.panel.y + geometry.panel.height - 25.0,
        geometry.panel.width - 210.0, Theme::badgeSize(), foreground, panelAlpha * 0.30, damage);
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
        }

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
    data.round    = Theme::windowRadius();
    data.clipBox  = clipBox;
    data.surface  = surface;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
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

        drawRect(row, selected ? withAlpha(accent, alpha * 0.18) : Theme::searchRowFill(false, static_cast<float>(alpha)), damage, Theme::inputRadius());
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
        drawRect(emptyBox, Theme::searchRowFill(false, static_cast<float>(alpha)), damage, Theme::inputRadius());
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
    return hitTestPreferencesPanel(computePreferencesPanel(frame->bounds), localX, localY);
}

PointerAction OverlayRenderer::applyPreference(PreferenceControl control, int value) {
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
    switch (control) {
    case PreferenceControl::WorkspaceView:
        if (value == 0)
                state.workspaceView = WorkspaceViewPreference::Stage;
        else if (value == 1)
                state.workspaceView = WorkspaceViewPreference::WorkspaceWall;
        else
                state.workspaceView = effectiveLayoutMode() == LayoutMode::Stage ? WorkspaceViewPreference::WorkspaceWall : WorkspaceViewPreference::Stage;
        break;
    case PreferenceControl::WindowView:
        if (value == 0)
                state.windowView = WindowViewPreference::Spatial;
        else if (value == 1)
                state.windowView = WindowViewPreference::Grouped;
        else
                state.windowView = state.windowView == WindowViewPreference::Grouped ? WindowViewPreference::Spatial : WindowViewPreference::Grouped;
        break;
    case PreferenceControl::Accent:
        if (value >= 0 && value <= 3)
                state.accent = static_cast<AccentPreference>(value);
        else
                switch (state.accent) {
                    case AccentPreference::FollowConfig: state.accent = AccentPreference::Green; break;
                    case AccentPreference::Green: state.accent = AccentPreference::Blue; break;
                    case AccentPreference::Blue: state.accent = AccentPreference::Violet; break;
                    case AccentPreference::Violet: state.accent = AccentPreference::FollowConfig; break;
            }
        break;
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

void OverlayRenderer::rebuildAfterPreferenceChange() {
    m_mode = defaultOverviewMode();
    m_applicationFilter.clear();
    m_previousFrames = m_frames;
    rebuildFrames();
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
    switch (m_preferences.state().accent) {
    case AccentPreference::Green:
        return {0.31F, 0.66F, 0.48F, 1.0F};
    case AccentPreference::Blue:
        return {0.24F, 0.63F, 0.96F, 1.0F};
    case AccentPreference::Violet:
        return {0.67F, 0.46F, 0.94F, 1.0F};
    case AccentPreference::FollowConfig:
        break;
    }

    if (const auto configured = m_config.accentColorOverride())
        return *configured;

    // "Theme" deliberately means the active Omarchy palette, not the currently focused
    // application's border. The palette is refreshed every time the overview opens.
    const auto& accent = m_config.palette().accent;
    return {accent.red, accent.green, accent.blue, accent.alpha};
}

LayoutMode OverlayRenderer::effectiveLayoutMode() const {
    switch (m_preferences.state().workspaceView) {
    case WorkspaceViewPreference::Stage:
        return LayoutMode::Stage;
    case WorkspaceViewPreference::WorkspaceWall:
        return LayoutMode::WorkspaceWall;
        case WorkspaceViewPreference::FollowConfig: return m_config.layoutMode();
    }
    return m_config.layoutMode();
}

int OverlayRenderer::effectiveAnimationDurationMs() const {
    const auto configured = m_config.animationDurationMs();
    switch (m_preferences.state().motion) {
        case MotionPreference::Reduced: return std::min(configured, 90);
    case MotionPreference::Off:
        return 0;
    case MotionPreference::FollowConfig:
        return configured;
    }
    return configured;
}

OverviewMode OverlayRenderer::defaultOverviewMode() const {
    return m_preferences.state().windowView == WindowViewPreference::Grouped ? OverviewMode::Grouped : OverviewMode::Spatial;
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
