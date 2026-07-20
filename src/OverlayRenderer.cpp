#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/Log.hpp>
#include <hypr-radiant/SearchPanelGeometry.hpp>
#include <hypr-radiant/Theme.hpp>

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
#include <cctype>
#include <cmath>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hypr_radiant {
namespace {

bool sameTarget(OverviewTarget lhs, OverviewTarget rhs) {
    return lhs.type == rhs.type && lhs.workspaceId == rhs.workspaceId && lhs.windowId == rhs.windowId;
}

CBox boxFor(const LayoutRect& rect) {
    return CBox{std::round(rect.x), std::round(rect.y), std::round(rect.width), std::round(rect.height)};
}

LayoutRect scaledAroundCenter(const LayoutRect& rect, double scale, double offsetY = 0.0) {
    const auto width = rect.width * scale;
    const auto height = rect.height * scale;
    return {
        .x = rect.x - (width - rect.width) / 2.0,
        .y = rect.y - (height - rect.height) / 2.0 + offsetY,
        .width = width,
        .height = height,
    };
}

LayoutRect remapRect(const LayoutRect& child, const LayoutRect& source, const LayoutRect& target) {
    if (source.width <= 0.0 || source.height <= 0.0)
        return child;

    return {
        .x = target.x + (child.x - source.x) * target.width / source.width,
        .y = target.y + (child.y - source.y) * target.height / source.height,
        .width = child.width * target.width / source.width,
        .height = child.height * target.height / source.height,
    };
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

CHyprColor tintedSurface(CHyprColor surface, CHyprColor tint, double amount) {
    const auto mix = static_cast<float>(std::clamp(amount, 0.0, 1.0));
    surface.r = std::lerp(surface.r, tint.r, mix);
    surface.g = std::lerp(surface.g, tint.g, mix);
    surface.b = std::lerp(surface.b, tint.b, mix);
    return surface;
}

std::string normalizedAppClass(std::string appClass) {
    std::ranges::transform(appClass, appClass.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return appClass;
}

bool classContains(const std::string& appClass, std::string_view candidate) {
    return appClass.find(candidate) != std::string::npos;
}

std::string appGlyph(const std::string& appClass) {
    const auto normalized = normalizedAppClass(appClass);
    if (classContains(normalized, "firefox") || classContains(normalized, "zen"))
        return "󰈹";
    if (classContains(normalized, "chrom") || classContains(normalized, "browser"))
        return "󰊯";
    if (classContains(normalized, "code") || classContains(normalized, "editor") || classContains(normalized, "opencode"))
        return "󰨞";
    if (classContains(normalized, "foot") || classContains(normalized, "kitty") || classContains(normalized, "ghostty") ||
        classContains(normalized, "alacritty") || classContains(normalized, "term"))
        return "";
    if (classContains(normalized, "discord") || classContains(normalized, "slack") || classContains(normalized, "chat"))
        return "󰙯";
    if (classContains(normalized, "nautilus") || classContains(normalized, "thunar") || classContains(normalized, "file"))
        return "󰉋";
    if (classContains(normalized, "spotify") || classContains(normalized, "music"))
        return "󰎆";
    return "󰣆";
}

CHyprColor appSignalColor(const std::string& appClass, CHyprColor inheritedAccent) {
    const auto normalized = normalizedAppClass(appClass);
    const auto inherited = [inheritedAccent](CHyprColor color, double amount) {
        color = tintedSurface(color, inheritedAccent, amount);
        color.a = 1.0F;
        return color;
    };

    if (classContains(normalized, "firefox") || classContains(normalized, "zen"))
        return inherited(CHyprColor{1.0F, 0.43F, 0.20F, 1.0F}, 0.12);
    if (classContains(normalized, "chrom") || classContains(normalized, "browser"))
        return inherited(CHyprColor{0.28F, 0.57F, 0.96F, 1.0F}, 0.12);
    if (classContains(normalized, "code") || classContains(normalized, "editor") || classContains(normalized, "opencode"))
        return inherited(CHyprColor{0.12F, 0.68F, 0.96F, 1.0F}, 0.16);
    if (classContains(normalized, "foot") || classContains(normalized, "kitty") || classContains(normalized, "ghostty") ||
        classContains(normalized, "alacritty") || classContains(normalized, "term")) {
        inheritedAccent.a = 1.0F;
        return inheritedAccent;
    }
    if (classContains(normalized, "discord") || classContains(normalized, "slack") || classContains(normalized, "chat"))
        return inherited(CHyprColor{0.42F, 0.45F, 0.96F, 1.0F}, 0.15);
    if (classContains(normalized, "nautilus") || classContains(normalized, "thunar") || classContains(normalized, "file"))
        return inherited(CHyprColor{0.83F, 0.78F, 0.52F, 1.0F}, 0.18);
    if (classContains(normalized, "spotify") || classContains(normalized, "music"))
        return inherited(CHyprColor{0.16F, 0.82F, 0.38F, 1.0F}, 0.14);

    static const std::array<CHyprColor, 5> palette{
        CHyprColor{0.38F, 0.74F, 0.98F, 1.0F},
        CHyprColor{0.68F, 0.55F, 0.98F, 1.0F},
        CHyprColor{0.31F, 0.84F, 0.70F, 1.0F},
        CHyprColor{0.95F, 0.62F, 0.34F, 1.0F},
        CHyprColor{0.93F, 0.47F, 0.66F, 1.0F},
    };
    std::uint32_t hash = 2166136261U;
    for (const auto character : normalized) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 16777619U;
    }
    return inherited(palette[hash % palette.size()], 0.22);
}

const MonitorSnapshot* findMonitorSnapshot(const RadiantState& state, std::int64_t id) {
    for (const auto& monitor : state.monitors) {
        if (monitor.id == id)
            return &monitor;
    }

    return nullptr;
}

bool contains(const LayoutRect& rect, double x, double y) {
    return rect.width > 0.0 && rect.height > 0.0 && x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
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

    for (const auto& window : g_pCompositor->m_windows) {
        if (!window || window->m_stableID != stableId || !window->m_isMapped)
            continue;

        return window;
    }

    return nullptr;
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
    snapshot.scale    = monitor->m_scale;

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

bool intersects(const LayoutRect& lhs, const LayoutRect& rhs) {
    return lhs.width > 0.0 && lhs.height > 0.0 && rhs.width > 0.0 && rhs.height > 0.0 && lhs.x < rhs.x + rhs.width &&
        lhs.x + lhs.width > rhs.x && lhs.y < rhs.y + rhs.height && lhs.y + lhs.height > rhs.y;
}

std::size_t selectedSearchIndex(const std::vector<OverviewTarget>& targets, OverviewTarget selected) {
    const auto found = std::ranges::find_if(targets, [selected](OverviewTarget target) { return sameTarget(target, selected); });
    return found == targets.end() ? 0 : static_cast<std::size_t>(std::distance(targets.begin(), found));
}

std::size_t visibleSearchStart(const std::vector<OverviewTarget>& targets, OverviewTarget selected, std::size_t capacity) {
    if (targets.size() <= capacity)
        return 0;

    const auto selectedIndex = selectedSearchIndex(targets, selected);
    if (selectedIndex < capacity)
        return 0;

    return std::min(selectedIndex - capacity + 1, targets.size() - capacity);
}

} // namespace

OverlayRenderer::OverlayRenderer(const RadiantConfig& config) : m_config(config) {}

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

void OverlayRenderer::show(RadiantState state) {
    m_mode = OverviewMode::Spatial;
    m_applicationFilter.clear();
    m_state = std::move(state);
    resetPointerInteraction();
    m_previousFrames.clear();
    clearSearch();
    rebuildFrames();

    if (const auto* frame = activeMonitorFrame()) {
        m_selectedFrameMonitorId = frame->monitorId;
        m_selectedTarget         = m_hitTester.initialSelection(*frame);
    } else {
        m_selectedFrameMonitorId = -1;
        m_selectedTarget         = {};
    }

    m_textures.clear();
    m_animation.animateTo(true, m_config.animationDurationMs());
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * 0.78))));
    animateSelection();
    damageAllMonitors();
}

void OverlayRenderer::showAppExpose(RadiantState state, std::string applicationClass) {
    m_mode = OverviewMode::AppExpose;
    m_applicationFilter = std::move(applicationClass);
    m_state = std::move(state);
    resetPointerInteraction();
    m_previousFrames.clear();
    clearSearch();
    rebuildFrames();

    if (const auto* frame = activeMonitorFrame()) {
        m_selectedFrameMonitorId = frame->monitorId;
        if (!frame->stage.windows.empty()) {
            const auto& window = frame->stage.windows.front();
            m_selectedTarget = {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId};
        } else {
            m_selectedTarget = m_hitTester.initialSelection(*frame);
        }
    }

    m_textures.clear();
    m_animation.animateTo(true, m_config.animationDurationMs());
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, m_config.animationDurationMs());
    animateSelection();
    damageAllMonitors();
}

void OverlayRenderer::toggle(RadiantState state) {
    if (m_animation.targetVisible()) {
        m_state = std::move(state);
        rebuildFrames();
        clearSearch();
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
    m_selectedTarget = m_hitTester.moveSelection(*frame, m_selectedTarget, direction);
    m_selectedFrameMonitorId = frame->monitorId;
    if (!sameTarget(previousTarget, m_selectedTarget))
        animateSelection();
    if (m_config.layoutMode() == LayoutMode::Stage && m_selectedTarget.workspaceId != previousWorkspace) {
        m_previousFrames = m_frames;
        rebuildFrames();
        m_stageTransition.hideImmediate();
        m_stageTransition.animateTo(true, std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * 0.78))));
    }
    damageAllMonitors();
}

void OverlayRenderer::selectTargetAt(double x, double y) {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return;

    const auto target = !m_searchActive ? m_hitTester.hitTest(*frame, localX, localY) : searchTargetAt(*frame, localX, localY);
    if (target.type == OverviewTargetType::None)
        return;

    if (frame->monitorId == m_selectedFrameMonitorId && sameTarget(m_selectedTarget, target))
        return;

    const auto previousWorkspace = m_selectedTarget.workspaceId;
    m_selectedTarget = target;
    m_selectedFrameMonitorId = frame->monitorId;
    animateSelection();
    if (!m_searchActive && m_config.layoutMode() == LayoutMode::Stage && target.workspaceId != previousWorkspace) {
        m_previousFrames = m_frames;
        rebuildFrames();
        m_stageTransition.hideImmediate();
        m_stageTransition.animateTo(true, std::max(0, static_cast<int>(std::round(m_config.animationDurationMs() * 0.78))));
    }
    damageAllMonitors();
}

void OverlayRenderer::pointerMoved(double x, double y) {
    m_pointerPosition = {.x = x, .y = y};
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
        action = {.type = PointerActionType::MoveWindow, .target = m_dragTarget, .windowId = m_pointerDownTarget.windowId};
    } else if (!m_dragging) {
        const auto releasedTarget = hitTest(x, y);
        if (sameTarget(releasedTarget, m_pointerDownTarget))
            action = {.type = PointerActionType::Activate, .target = releasedTarget};
    }

    resetPointerInteraction();
    damageAllMonitors();
    return action;
}

void OverlayRenderer::refresh(RadiantState state) {
    m_state = std::move(state);
    m_previousFrames = m_frames;
    rebuildFrames();
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, m_config.animationDurationMs());
    animateSelection();
    m_textures.clear();
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
    m_stageTransition.hideImmediate();
    m_stageTransition.animateTo(true, m_config.animationDurationMs());
    animateSelection();
    m_textures.clear();
    damageAllMonitors();
}

void OverlayRenderer::hideImmediate() {
    const auto wasRenderable = m_animation.renderable();
    m_animation.hideImmediate();
    m_stageTransition.hideImmediate();
    m_selectionTransition.hideImmediate();
    m_frames.clear();
    m_previousFrames.clear();
    m_frameBoundsByMonitor.clear();
    m_textures.clear();
    m_selectedTarget = {};
    m_selectedFrameMonitorId = -1;
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

    return !m_searchActive ? m_hitTester.hitTest(*frame, localX, localY) : searchTargetAt(*frame, localX, localY);
}

void OverlayRenderer::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_LAST_MOMENT || !m_animation.renderable())
        return;

    const auto alpha = std::clamp(static_cast<float>(m_animation.value()), 0.0F, 1.0F);

    if (alpha > 0.001F)
        renderCurrentMonitor(alpha);

    if (m_animation.running() || m_stageTransition.running() || m_selectionTransition.running())
        damageAllMonitors();
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

    auto backdrop = m_config.layoutMode() == LayoutMode::Stage ? m_config.backgroundColor() : Theme::backdropColor();
    if (m_config.layoutMode() == LayoutMode::Stage)
        backdrop.a = Theme::stageBackdropColor().a;
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

    const auto withAlpha = [](CHyprColor color, double multiplier) {
        color.a *= multiplier;
        return color;
    };
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

    const auto titleX = hasContent ? contentLeft : (mode == LayoutMode::Stage ? 48.0 : 46.0);
    const auto titleY = hasContent ? std::max(34.0, contentTop - 58.0) : 34.0;
    if (hasContent && mode != LayoutMode::Stage) {
        const auto stageBox = CBox{
            std::max(18.0, contentLeft - 44.0),
            std::max(18.0, titleY - 24.0),
            std::min(frame.bounds.width - 36.0, contentRight - contentLeft + 88.0),
            std::min(frame.bounds.height - 36.0, contentBottom - titleY + 62.0),
        };
        drawRect(CBox{stageBox.x + Theme::shadowOffsetX(), stageBox.y + Theme::shadowOffsetY(), stageBox.w, stageBox.h}, shadowColor, damage, 34);
        drawRect(stageBox, withAlpha(Theme::panelColor(), contentAlpha), damage, 32, true);
    }
    renderLabel("Workspaces", titleX, titleY, std::max(1.0, frame.bounds.width * 0.45), Theme::titleSize(), contentAlpha, damage);
    if (!searchActive && hasContent) {
        const auto helpWidth = std::min(680.0, std::max(1.0, contentRight - contentLeft));
        const auto helpX     = contentLeft + centered(contentRight - contentLeft, helpWidth);
        renderLabel("\xe2\x86\x91\xe2\x86\x93\xe2\x86\x90\xe2\x86\x92 navigate \xc2\xb7 Enter activate \xc2\xb7 1-9 jump \xc2\xb7 / search \xc2\xb7 Esc close",
            helpX, frame.bounds.height - 16.0 - 24.0 + 4.0, helpWidth, Theme::hintSize(), contentAlpha * 0.68, damage);
    }

    bool   hasDock    = false;
    double dockLeft   = frame.bounds.width;
    double dockTop    = frame.bounds.height;
    double dockRight  = 0.0;
    double dockBottom = 0.0;
    for (const auto& workspace : frame.workspaces) {
        if (workspace.rect.height > 120.0 || workspace.rect.width <= 0.0)
            continue;
        hasDock    = true;
        dockLeft   = std::min(dockLeft, workspace.rect.x);
        dockTop    = std::min(dockTop, workspace.rect.y);
        dockRight  = std::max(dockRight, workspace.rect.x + workspace.rect.width);
        dockBottom = std::max(dockBottom, workspace.rect.y + workspace.rect.height);
    }

    if (hasDock && mode == LayoutMode::Stage) {
        const auto dockBox = CBox{dockLeft - 12.0, dockTop - 10.0, dockRight - dockLeft + 24.0, dockBottom - dockTop + 20.0};
        drawRect(CBox{dockBox.x + Theme::shadowOffsetX(), dockBox.y + Theme::shadowOffsetY(), dockBox.w, dockBox.h}, shadowColor, damage, 18);
        drawRect(dockBox, withAlpha(Theme::panelColor(), contentAlpha), damage, 16);
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
            CBorderPassElement::SBorderData border;
            border.box        = workspaceBox;
            const auto borderColor = Theme::cardBorder(workspace.active, workspaceSelected, static_cast<float>(contentAlpha));
            border.grad1      = Config::CGradientValueData{borderColor};
            border.a          = static_cast<float>(borderColor.a);
            border.round      = round;
            border.borderSize = 2;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
        }

        if (workspace.active) {
            const auto accentWidth = std::min(compact ? 28.0 : 56.0, std::max(1.0, workspaceBox.w - 32.0));
            const auto accentBar   = withAlpha(Theme::accentColor(), contentAlpha);
            drawRect(CBox{workspaceBox.x + centered(workspaceBox.w, accentWidth), workspaceBox.y + workspaceBox.h - 7.0, accentWidth, 4.0},
                accentBar, damage, 2);
        }

        renderLabel(workspace.name, workspace.rect.x + (compact ? 10.0 : 18.0), workspace.rect.y + (compact ? 8.0 : 14.0),
            std::max(1.0, workspace.rect.width - (compact ? 20.0 : 36.0)), compact ? Theme::hintSize() : Theme::labelSize(), contentAlpha, damage);

        if (workspace.empty && !compact)
            renderLabel("Empty", workspace.rect.x + 18.0, workspace.rect.y + 44.0, std::max(1.0, workspace.rect.width - 36.0), Theme::footerSize(), contentAlpha * 0.42, damage);

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
                renderLabel(window.label, window.rect.x + 14.0, window.rect.y + window.rect.height - footerHeight + 8.0,
                    std::max(1.0, window.rect.width - 28.0), Theme::footerSize(), contentAlpha, damage);
            } else {
                renderLabel(window.label, window.rect.x + (compact ? 8.0 : 12.0), window.rect.y + (compact ? 5.0 : 8.0),
                    std::max(1.0, window.rect.width - (compact ? 16.0 : 20.0)), compact ? Theme::badgeSize() : Theme::footerSize(), contentAlpha, damage);
            }

            if (windowSelected) {
                CBorderPassElement::SBorderData border;
                border.box        = CBox{windowBox.x - 5.0, windowBox.y - 5.0, windowBox.w + 10.0, windowBox.h + 10.0};
                const auto borderColor = Theme::windowBorder(true, static_cast<float>(contentAlpha));
                border.grad1      = Config::CGradientValueData{borderColor};
                border.a          = static_cast<float>(borderColor.a);
                border.round      = windowRound + 5;
                border.borderSize = 3;
                g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
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

void OverlayRenderer::renderStageFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    const auto withAlpha = [](CHyprColor color, double multiplier) {
        color.a *= multiplier;
        return color;
    };

    const auto searchMultiplier = m_searchActive ? 0.16 : 1.0;
    const auto monitorMultiplier = m_selectedFrameMonitorId == -1 || frame.monitorId == m_selectedFrameMonitorId ? 1.0 : 0.72;
    const auto contentAlpha = alpha * searchMultiplier * monitorMultiplier;
    const auto accent       = resolvedAccentColor();
    const auto background   = m_config.backgroundColor();
    const auto railSurface  = tintedSurface(Theme::railColor(), background, 0.46);
    const auto stageSurface = tintedSurface(Theme::stageWindowFill(1.0F), background, 0.34);
    auto railBox            = boxFor(frame.rail.bounds);
    const auto transition   = std::clamp(m_stageTransition.value(), 0.0, 1.0);
    const auto selectionTransition = std::clamp(m_selectionTransition.value(), 0.0, 1.0);
    const auto railEntranceOffset = -(1.0 - alpha) * 18.0;
    railBox.y += railEntranceOffset;
    const auto previousFrameIt = std::ranges::find_if(m_previousFrames, [&frame](const WorkspaceWallFrame& candidate) {
        return candidate.monitorId == frame.monitorId;
    });
    const auto* previousFrame = previousFrameIt == m_previousFrames.end() ? nullptr : &*previousFrameIt;

    auto gridColor = accent;
    gridColor.a *= static_cast<float>(contentAlpha * 0.028);
    for (double x = 0.0; x < frame.bounds.width; x += 160.0)
        drawRect(CBox{x, frame.stage.bounds.y - 24.0, 1.0, frame.stage.bounds.height + 30.0}, gridColor, damage);
    for (double y = frame.stage.bounds.y; y < frame.stage.bounds.y + frame.stage.bounds.height; y += 96.0)
        drawRect(CBox{0.0, y, frame.bounds.width, 1.0}, gridColor, damage);

    drawRect(CBox{railBox.x + 7.0, railBox.y + 10.0, railBox.w, railBox.h}, withAlpha(Theme::shadowColor(), contentAlpha * 0.72), damage,
        Theme::railRadius() + 2);
    drawRect(railBox, withAlpha(railSurface, contentAlpha), damage, Theme::railRadius(), true);
    drawRect(CBox{railBox.x + 28.0, railBox.y + 1.0, std::max(1.0, railBox.w - 56.0), 1.0},
        withAlpha(accent, contentAlpha * 0.22), damage, 1);
    if (railBox.x >= 170.0)
        renderLabel(std::format("󰍹  SPACES // {:02}", frame.workspaces.size()), 42.0, railBox.y + 16.0,
            railBox.x - 70.0, Theme::hintSize(), contentAlpha * 0.68, damage);

    if (g_pHyprRenderer && railBox.w > 0.0 && railBox.h > 0.0) {
        CBorderPassElement::SBorderData border;
        border.box        = railBox;
        border.grad1      = Config::CGradientValueData{withAlpha(accent, contentAlpha * 0.20)};
        border.a          = static_cast<float>(accent.a * contentAlpha * 0.20);
        border.round      = Theme::railRadius();
        border.borderSize = 1;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
    }

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
        if (selected)
            displayRect = scaledAroundCenter(displayRect, std::lerp(0.99, 1.035, selectionTransition), -4.0 * selectionTransition);
        const auto cardBox  = boxFor(displayRect);
        const auto radius   = Theme::workspaceRadius(true);

        if (selected) {
            drawRect(CBox{cardBox.x - 5.0, cardBox.y - 5.0, cardBox.w + 10.0, cardBox.h + 10.0},
                withAlpha(accent, contentAlpha * 0.14), damage, radius + 5);
        }

        drawRect(CBox{cardBox.x + 3.0, cardBox.y + 5.0, cardBox.w, cardBox.h}, withAlpha(Theme::shadowColor(), contentAlpha * 0.55), damage, radius);
        auto cardFill = tintedSurface(Theme::railCardFill(selected, workspace.empty, static_cast<float>(contentAlpha)), background, 0.30);
        if (selected && !workspace.createTarget)
            cardFill = tintedSurface(cardFill, accent, 0.22);
        drawRect(cardBox, cardFill, damage, radius);

        if (workspace.createTarget)
            renderLabel("+", cardBox.x + centered(cardBox.w, 24.0), cardBox.y + centered(cardBox.h, 28.0),
                24.0, Theme::titleSize(), contentAlpha * (selected ? 1.0 : 0.62), damage);

        for (const auto& window : workspace.windows) {
            const auto previewBox = boxFor(remapRect(window.rect, workspace.rect, displayRect));
            renderWindowPreview(window, previewBox, contentAlpha, damage);
        }

        if (selected || workspace.active) {
            CBorderPassElement::SBorderData border;
            border.box        = cardBox;
            border.grad1      = Config::CGradientValueData{withAlpha(accent, contentAlpha * (selected ? 1.0 : 0.34))};
            border.a          = static_cast<float>(accent.a * contentAlpha * (selected ? 1.0 : 0.34));
            border.round      = radius;
            border.borderSize = selected ? 2 : 1;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
        }

        if (workspace.active && !selected) {
            const auto lineWidth = std::min(42.0, std::max(12.0, cardBox.w - 24.0));
            drawRect(CBox{cardBox.x + centered(cardBox.w, lineWidth), cardBox.y + cardBox.h - 5.0, lineWidth, 3.0},
                withAlpha(accent, contentAlpha * 0.72), damage, 2);
        }

        const auto label = workspace.createTarget ? std::format("NEW [{:02}]", workspace.workspaceId) :
            workspace.name == std::to_string(workspace.workspaceId) ? std::format("[{:02}] · {}W", workspace.workspaceId, workspace.windows.size()) :
            std::format("[{:02}] {} · {}W", workspace.workspaceId, workspace.name, workspace.windows.size());
        renderLabel(label, displayRect.x + 4.0, displayRect.y + displayRect.height + 8.0,
            std::max(1.0, displayRect.width - 8.0), Theme::hintSize(), contentAlpha * (selected ? 1.0 : 0.64), damage);
    }

    if (frame.rail.overflowLeft)
        renderLabel("\xe2\x80\xb9", frame.rail.bounds.x + 5.0, frame.rail.bounds.y + railEntranceOffset + frame.rail.bounds.height / 2.0 - 10.0,
            20.0, Theme::titleSize(), contentAlpha * 0.72, damage);
    if (frame.rail.overflowRight)
        renderLabel("\xe2\x80\xba", frame.rail.bounds.x + frame.rail.bounds.width - 20.0,
            frame.rail.bounds.y + railEntranceOffset + frame.rail.bounds.height / 2.0 - 10.0, 16.0, Theme::titleSize(), contentAlpha * 0.72, damage);

    const auto stageAlpha = contentAlpha * transition;
    const auto stageOffset = (1.0 - transition) * 10.0;

    if (previousFrame && previousFrame->stage.workspaceId != frame.stage.workspaceId && transition < 1.0) {
        const auto previousAlpha = contentAlpha * (1.0 - transition);
        const auto previousOffset = -transition * 6.0;
        for (const auto& window : previousFrame->stage.windows) {
            auto previousBox = boxFor(window.rect);
            previousBox.y += previousOffset;
            const auto radius = Theme::windowRadius();
            drawRect(CBox{previousBox.x + 7.0, previousBox.y + 10.0, previousBox.w, previousBox.h},
                withAlpha(Theme::shadowColor(), previousAlpha * 0.72), damage, radius + 2);
            drawRect(previousBox, Theme::stageWindowFill(static_cast<float>(previousAlpha)), damage, radius);
            renderWindowPreview(window, previousBox, previousAlpha, damage);
        }
    }
    const auto stageLabel = m_mode == OverviewMode::AppExpose ? std::format("󰀻  APP EXPOSE // {}", frame.stage.name) :
        frame.stage.name == std::to_string(frame.stage.workspaceId) ? std::format("󰍹  DESKTOP // [{:02}]", frame.stage.workspaceId) :
        std::format("󰍹  DESKTOP // [{:02}] {}", frame.stage.workspaceId, frame.stage.name);
    renderLabel(stageLabel, frame.stage.bounds.x, frame.stage.bounds.y - 28.0 + stageOffset,
        std::max(1.0, frame.stage.bounds.width * 0.5), Theme::labelSize(), stageAlpha * 0.78, damage);

    if (frame.stage.empty) {
        renderLabel("Empty workspace", frame.stage.bounds.x + centered(frame.stage.bounds.width, 180.0),
            frame.stage.bounds.y + centered(frame.stage.bounds.height, 24.0) + stageOffset, 180.0, Theme::footerSize(), stageAlpha * 0.42, damage);
    }

    for (const auto& window : frame.stage.windows) {
        const auto selected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
            m_selectedTarget, {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
        auto displayRect = window.rect;
        displayRect.y += stageOffset;
        if (selected)
            displayRect = scaledAroundCenter(displayRect, std::lerp(0.995, 1.014, selectionTransition), -3.0 * selectionTransition);
        const auto windowBox = boxFor(displayRect);
        const auto radius = Theme::windowRadius();

        if (window.appGroupStart) {
            const auto glyphColor = appSignalColor(window.appClass, accent);
            renderColoredLabel(appGlyph(window.appClass), displayRect.x + 2.0, displayRect.y - 22.0,
                18.0, Theme::hintSize(), glyphColor, stageAlpha * 0.92, damage);
            renderLabel(window.appClass, displayRect.x + 24.0, displayRect.y - 22.0,
                std::max(1.0, displayRect.width - 26.0), Theme::hintSize(), stageAlpha * 0.68, damage);
        }

        const auto windowAlpha = m_dragging && window.stableId == m_pointerDownTarget.windowId ? stageAlpha * 0.30 : stageAlpha;
        drawRect(CBox{windowBox.x + 7.0, windowBox.y + 10.0, windowBox.w, windowBox.h},
            withAlpha(Theme::shadowColor(), windowAlpha * 0.72), damage, radius + 2);
        if (selected) {
            drawRect(CBox{windowBox.x - 6.0, windowBox.y - 6.0, windowBox.w + 12.0, windowBox.h + 12.0},
                withAlpha(accent, stageAlpha * 0.16), damage, radius + 6);
        }
        drawRect(windowBox, withAlpha(stageSurface, windowAlpha), damage, radius);
        renderWindowPreview(window, windowBox, windowAlpha, damage);

        if (selected) {
            CBorderPassElement::SBorderData border;
            border.box        = CBox{windowBox.x - 3.0, windowBox.y - 3.0, windowBox.w + 6.0, windowBox.h + 6.0};
            border.grad1      = Config::CGradientValueData{withAlpha(accent, stageAlpha)};
            border.a          = static_cast<float>(accent.a * stageAlpha);
            border.round      = radius + 3;
            border.borderSize = 3;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));

            const auto state = window.fullscreen ? "FULL" : window.floating ? "FLOAT" : "TILED";
            if (windowBox.w >= 112.0 && windowBox.h >= 56.0) {
                const auto badge = CBox{windowBox.x + windowBox.w - 74.0, windowBox.y + 10.0, 64.0, 22.0};
                drawRect(badge, withAlpha(railSurface, stageAlpha * 0.88), damage, 7, true);
                drawRect(CBox{badge.x + 8.0, badge.y + 10.0, 4.0, 4.0}, withAlpha(accent, stageAlpha), damage, 2);
                renderLabel(state, badge.x + 18.0, badge.y + 5.0, 40.0, Theme::badgeSize(), stageAlpha * 0.92, damage);
            }
        }

        const auto titleUpper = std::max(1.0, std::min(380.0, frame.stage.bounds.width - 24.0));
        const auto titleLower = std::min(136.0, titleUpper);
        const auto titleWidth = std::clamp(84.0 + static_cast<double>(window.label.size()) * 6.2, titleLower, titleUpper);
        const auto stageRight = frame.stage.bounds.x + frame.stage.bounds.width;
        const auto titleX = std::clamp(windowBox.x + centered(windowBox.w, titleWidth), frame.stage.bounds.x, std::max(frame.stage.bounds.x, stageRight - titleWidth));
        const auto titleY = std::min(windowBox.y + windowBox.h + 8.0, frame.stage.bounds.y + frame.stage.bounds.height - 26.0);
        const auto titleBox = CBox{titleX, titleY, titleWidth, 26.0};
        auto titleSurface = tintedSurface(railSurface, accent, selected ? 0.18 : 0.04);
        drawRect(CBox{titleBox.x + 3.0, titleBox.y + 4.0, titleBox.w, titleBox.h}, withAlpha(Theme::shadowColor(), stageAlpha * 0.52), damage, 9);
        drawRect(titleBox, withAlpha(titleSurface, stageAlpha * (selected ? 0.96 : 0.78)), damage, 9, true);
        if (selected)
            drawRect(CBox{titleBox.x + 12.0, titleBox.y + titleBox.h - 1.0, std::max(1.0, titleBox.w - 24.0), 1.0}, withAlpha(accent, stageAlpha * 0.64), damage, 1);
        renderColoredLabel(appGlyph(window.appClass), titleBox.x + 11.0, titleBox.y + 6.0,
            18.0, Theme::hintSize(), appSignalColor(window.appClass, accent), stageAlpha * (selected ? 1.0 : 0.82), damage);
        renderLabel(window.label, titleBox.x + 33.0, titleBox.y + 6.0,
            std::max(1.0, titleBox.w - 45.0), Theme::hintSize(), stageAlpha * (selected ? 1.0 : 0.76), damage);
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
            drawRect(CBox{ghost.x + 9.0, ghost.y + 12.0, ghost.w, ghost.h}, withAlpha(Theme::shadowColor(), contentAlpha), damage, Theme::windowRadius() + 3);
            drawRect(ghost, Theme::stageWindowFill(static_cast<float>(contentAlpha * 0.96)), damage, Theme::windowRadius());
            renderWindowPreview(*source, ghost, contentAlpha * 0.96, damage);
        }
    }

    if (!m_searchActive) {
        const auto modeLabel = m_mode == OverviewMode::Grouped ? "Apps" : m_mode == OverviewMode::AppExpose ? "App Exposé" : "Spatial";
        const auto deckWidth = std::min(920.0, std::max(1.0, frame.bounds.width - 64.0));
        const auto deck = CBox{centered(frame.bounds.width, deckWidth), frame.bounds.height - 52.0, deckWidth, 34.0};
        drawRect(CBox{deck.x + 5.0, deck.y + 7.0, deck.w, deck.h}, withAlpha(Theme::shadowColor(), contentAlpha * 0.58), damage, 14);
        drawRect(deck, withAlpha(railSurface, contentAlpha * 0.84), damage, 14, true);
        if (g_pHyprRenderer) {
            CBorderPassElement::SBorderData border;
            border.box = deck;
            border.grad1 = Config::CGradientValueData{withAlpha(accent, contentAlpha * 0.16)};
            border.a = static_cast<float>(accent.a * contentAlpha * 0.16);
            border.round = 14;
            border.borderSize = 1;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));
        }
        renderLabel("← → navigate  ·  Tab view  ·  Enter open  ·  / search  ·  Esc close",
            deck.x + 18.0, deck.y + 9.0, std::max(1.0, deck.w - 138.0), Theme::hintSize(), contentAlpha * 0.66, damage);
        renderLabel(modeLabel, deck.x + deck.w - 104.0, deck.y + 9.0,
            88.0, Theme::hintSize(), contentAlpha * 0.84, damage);
    } else {
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

    const auto withAlpha = [](CHyprColor color, double multiplier) {
        color.a *= multiplier;
        return color;
    };
    const auto accent = resolvedAccentColor();
    const auto background = m_config.backgroundColor();

    const auto panelBox = CBox{geometry.panelX, geometry.panelY, geometry.panelW, geometry.panelH};
    const auto inputBox = CBox{geometry.inputX, geometry.inputY, geometry.inputW, geometry.inputH};

    drawRect(CBox{panelBox.x + Theme::shadowOffsetX(), panelBox.y + Theme::shadowOffsetY(), panelBox.w, panelBox.h},
        withAlpha(Theme::shadowColor(), alpha), damage, Theme::searchRadius());
    drawRect(panelBox, withAlpha(tintedSurface(Theme::searchPanelColor(), background, 0.34), alpha), damage, Theme::searchRadius(), true);

    CBorderPassElement::SBorderData border;
    border.box        = panelBox;
    const auto panelBorder = withAlpha(accent, alpha * 0.62);
    border.grad1      = Config::CGradientValueData{panelBorder};
    border.a          = static_cast<float>(panelBorder.a);
    border.round      = Theme::searchRadius();
    border.borderSize = 1;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));

    drawRect(inputBox, withAlpha(tintedSurface(Theme::searchInputColor(), background, 0.28), alpha), damage, Theme::inputRadius());

    const auto promptTexture = [this]() -> SP<Render::ITexture> {
        if (!g_pHyprRenderer)
            return nullptr;
        const std::string text = ">";
        const int         size = Theme::labelSize();
        const int         maxWidth = 32;
        const auto        key = std::format("{}:{}:{}", size, maxWidth, text);
        auto              it = m_textures.find(key);
        if (it == m_textures.end())
            it = m_textures.emplace(key, g_pHyprRenderer->renderText(text, m_config.foregroundColor(), size, false, m_config.fontFamily(), maxWidth)).first;
        return it->second;
    }();
    const auto promptWidth  = promptTexture && promptTexture->ok() ? promptTexture->m_size.x : 0.0;
    const auto promptHeight = promptTexture && promptTexture->ok() ? promptTexture->m_size.y : 0.0;
    const auto textY        = inputBox.y + std::round((inputBox.h - promptHeight) / 2.0);
    const auto promptX      = inputBox.x + 16.0;
    const auto queryX       = promptX + promptWidth + 10.0;

    renderLabel(">", promptX, textY, 32.0, Theme::labelSize(), alpha, damage);
    renderLabel(std::format("{}_", m_searchQuery), queryX, textY,
        std::max(1.0, inputBox.x + inputBox.w - 92.0 - queryX), Theme::labelSize(), alpha, damage);
    renderLabel(std::format("{} result{}", targets.size(), targets.size() == 1 ? "" : "s"),
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
            renderLabel("SPACE", row.x + 31.0, row.y + 23.0, 46.0, Theme::badgeSize(), alpha * 0.78, damage);
            renderLabel(std::format("Workspace {}", name), row.x + 104.0, row.y + 8.0, row.w - 128.0, Theme::labelSize(), alpha, damage);
            renderLabel("Switch to this workspace", row.x + 104.0, row.y + 30.0, row.w - 128.0, Theme::hintSize(), alpha * 0.50, damage);
        } else if (const auto* window = findWindowCard(target.windowId)) {
            drawRect(badgeBox, withAlpha(Theme::searchBadgeWindow(), alpha), damage, Theme::inputRadius());
            renderLabel("WINDOW", row.x + 26.0, row.y + 23.0, 56.0, Theme::badgeSize(), alpha * 0.78, damage);
            renderLabel(window->label, row.x + 104.0, row.y + 8.0, row.w - 128.0, Theme::labelSize(), alpha, damage);
            renderLabel(std::format("Workspace {}", window->workspaceId), row.x + 104.0, row.y + 30.0, row.w - 128.0, Theme::hintSize(), alpha * 0.50, damage);
        }
    }

    if (targets.empty()) {
        renderLabel("No results", inputBox.x + 4.0, geometry.resultsY + 18.0, inputBox.w - 8.0, Theme::titleSize(), alpha * 0.78, damage);
        renderLabel("Try another window title, workspace name, or number", inputBox.x + 4.0, geometry.resultsY + 48.0,
            inputBox.w - 8.0, Theme::hintSize(), alpha * 0.48, damage);
    }

    renderLabel("\xe2\x86\x91\xe2\x86\x93 select \xc2\xb7 Enter activate \xc2\xb7 Esc clear",
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
    for (const auto& frame : m_frames) {
        for (const auto& window : frame.stage.windows) {
            if (window.stableId == windowId)
                return &window;
        }
        for (const auto& workspace : frame.workspaces) {
            for (const auto& window : workspace.windows) {
                if (window.stableId == windowId)
                    return &window;
            }
        }
    }

    return nullptr;
}

const WorkspaceCard* OverlayRenderer::findWorkspaceCard(std::int64_t workspaceId) const noexcept {
    for (const auto& frame : m_frames) {
        for (const auto& workspace : frame.workspaces) {
            if (workspace.workspaceId == workspaceId)
                return &workspace;
        }
    }

    return nullptr;
}

void OverlayRenderer::renderLabel(const std::string& text, double x, double y, double maxWidth, int pointSize, double alpha, const CRegion& damage) {
    renderColoredLabel(text, x, y, maxWidth, pointSize, m_config.foregroundColor(), alpha, damage);
}

void OverlayRenderer::renderColoredLabel(
    const std::string& text, double x, double y, double maxWidth, int pointSize, CHyprColor color, double alpha, const CRegion& damage) {
    if (!g_pHyprRenderer || text.empty() || maxWidth <= 0.0 || alpha <= 0.001)
        return;

    // m_textures is an open-session text cache keyed by (pointSize, ceil(maxWidth), color, text).
    // It is cleared in show() and hideImmediate(), so repeated frames reuse the same labels
    // instead of accumulating across overview opens. Per session, growth is naturally bounded
    // by rendered workspace names (one per workspace card), visible window labels (one per
    // window card), fixed helpers (overview title, type/search hints, panel title, WINDOW,
    // SPACE, no-results text, workspace secondary text, and footer), search result labels,
    // and live search strings. The only per-keystroke key is std::format("{}_", m_searchQuery),
    // and appendSearchChar() caps that at 64 query lengths; backspace reuses shorter keys.
    // Result counts are bounded by the distinct match counts encountered. With W workspace cards,
    // V visible window cards, E empty non-compact workspace cards, and T <= W + V searchable
    // targets, a realistic session stays around 74 + E + 3W + 2V + T entries before duplicate
    // strings/max widths collapse further. The cache therefore cannot realistically grow
    // without bound during normal use; do not add eviction here unless future analysis changes
    // those inputs.
    const auto channel = [](float value) {
        return static_cast<int>(std::round(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    const auto key = std::format("{}:{}:{:02x}{:02x}{:02x}{:02x}:{}", pointSize, static_cast<int>(std::ceil(maxWidth)),
        channel(color.r), channel(color.g), channel(color.b), channel(color.a), text);
    auto       it  = m_textures.find(key);
    if (it == m_textures.end()) {
        it = m_textures.emplace(key, g_pHyprRenderer->renderText(text, color, pointSize, false,
            m_config.fontFamily(), static_cast<int>(maxWidth))).first;
    }

    const auto& texture = it->second;
    if (!texture || !texture->ok() || texture->m_size.x <= 0.0 || texture->m_size.y <= 0.0)
        return;

    CTexPassElement::SRenderData data;
    data.tex      = texture;
    data.box      = CBox{std::round(x), std::round(y), std::min(texture->m_size.x, maxWidth), texture->m_size.y};
    data.overallA = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    data.damage   = damage;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
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

    return Theme::accentColor();
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
