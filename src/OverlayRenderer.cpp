#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <algorithm>
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

LayoutRect globalBoundsForMonitor(const PHLMONITOR& monitor, RadiantSize renderSize) {
    return {
        .x      = monitor->m_position.x,
        .y      = monitor->m_position.y,
        .width  = renderSize.width,
        .height = renderSize.height,
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

WorkspaceWallOptions layoutOptionsFor(LayoutMode mode) {
    if (mode == LayoutMode::WorkspaceWall)
        return {};

    return WorkspaceWallOptions{
        .minimumWorkspaceSlots = 6,
        .outerPadding          = 120.0,
        .cardGap               = 24.0,
        .windowGap             = 12.0,
        .windowInset           = 22.0,
        .focusedStage          = true,
    };
}

struct SearchPanelGeometry {
    CBox        panel;
    CBox        input;
    double      resultsY = 0.0;
    double      rowHeight = 0.0;
    double      rowGap = 0.0;
    std::size_t capacity = 0;
};

SearchPanelGeometry searchPanelGeometry(const WorkspaceWallFrame& frame, std::size_t resultCount) {
    const auto margin         = std::clamp(frame.bounds.width * 0.04, 24.0, 72.0);
    const auto panelWidth     = std::min(760.0, std::max(1.0, frame.bounds.width - margin * 2.0));
    const auto maxPanelHeight = std::min(590.0, std::max(1.0, frame.bounds.height - margin * 2.0));
    const auto rowHeight      = 62.0;
    const auto rowGap         = 8.0;
    const auto fixedHeight    = 194.0;
    const auto maxCapacity    = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::max(0.0, maxPanelHeight - fixedHeight + rowGap) / (rowHeight + rowGap)));
    const auto capacity       = std::max<std::size_t>(1, std::min(resultCount, maxCapacity));
    const auto rowsHeight     = resultCount == 0 ? 82.0 :
        static_cast<double>(capacity) * rowHeight + static_cast<double>(capacity - 1) * rowGap;
    const auto panelHeight = std::min(maxPanelHeight, std::max(278.0, fixedHeight + rowsHeight));
    const auto panelX      = centered(frame.bounds.width, panelWidth);
    const auto panelY      = centered(frame.bounds.height, panelHeight);
    const auto inputInset  = std::min(28.0, panelWidth * 0.05);
    const auto inputHeight = 64.0;
    const auto inputY      = panelY + 58.0;
    const auto resultsY    = inputY + inputHeight + 18.0;

    return {
        .panel = {panelX, panelY, panelWidth, panelHeight},
        .input = {panelX + inputInset, inputY, std::max(1.0, panelWidth - inputInset * 2.0), inputHeight},
        .resultsY = resultsY,
        .rowHeight = rowHeight,
        .rowGap = rowGap,
        .capacity = capacity,
    };
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
    m_state = std::move(state);
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
    damageAllMonitors();
}

void OverlayRenderer::toggle(RadiantState state) {
    if (m_animation.targetVisible()) {
        m_state = std::move(state);
        rebuildFrames();
        clearSearch();
        m_animation.animateTo(false, m_config.animationDurationMs());
        damageAllMonitors();
        return;
    }

    show(std::move(state));
}

void OverlayRenderer::moveSelection(NavigationDirection direction) {
    if (!m_searchQuery.empty()) {
        moveSearchSelection(direction);
        return;
    }

    const auto* frame = frameForSelectedTarget();
    if (!frame)
        return;

    m_selectedTarget = m_hitTester.moveSelection(*frame, m_selectedTarget, direction);
    m_selectedFrameMonitorId = frame->monitorId;
    damageAllMonitors();
}

void OverlayRenderer::selectTargetAt(double x, double y) {
    double localX = x;
    double localY = y;
    const auto* frame = frameForPoint(x, y, localX, localY);
    if (!frame)
        return;

    const auto target = m_searchQuery.empty() ? m_hitTester.hitTest(*frame, localX, localY) : searchTargetAt(*frame, localX, localY);
    if (target.type == OverviewTargetType::None)
        return;

    if (frame->monitorId == m_selectedFrameMonitorId && sameTarget(m_selectedTarget, target))
        return;

    m_selectedTarget = target;
    m_selectedFrameMonitorId = frame->monitorId;
    damageAllMonitors();
}

void OverlayRenderer::appendSearchChar(char value) {
    if (m_searchQuery.size() >= 64)
        return;

    m_searchQuery.push_back(value);
    rebuildSearchMatches();
    selectFirstSearchMatch();
    damageAllMonitors();
}

void OverlayRenderer::backspaceSearch() {
    if (m_searchQuery.empty())
        return;

    m_searchQuery.pop_back();
    rebuildSearchMatches();
    selectFirstSearchMatch();
    if (m_searchQuery.empty() && m_selectedTarget.type == OverviewTargetType::None) {
        if (const auto* frame = activeMonitorFrame()) {
            m_selectedTarget = m_hitTester.initialSelection(*frame);
            m_selectedFrameMonitorId = frame->monitorId;
        }
    }
    damageAllMonitors();
}

void OverlayRenderer::clearSearchOrHide() {
    if (!m_searchQuery.empty()) {
        clearSearch();
        if (m_selectedTarget.type == OverviewTargetType::None) {
            if (const auto* frame = activeMonitorFrame()) {
                m_selectedTarget = m_hitTester.initialSelection(*frame);
                m_selectedFrameMonitorId = frame->monitorId;
            }
        }
        damageAllMonitors();
        return;
    }

    hideImmediate();
}

void OverlayRenderer::hideImmediate() {
    const auto wasRenderable = m_animation.renderable();
    m_animation.hideImmediate();
    m_frames.clear();
    m_frameBoundsByMonitor.clear();
    m_textures.clear();
    m_selectedTarget = {};
    m_selectedFrameMonitorId = -1;
    clearSearch();

    if (wasRenderable)
        damageAllMonitors();
}

bool OverlayRenderer::active() const noexcept {
    return m_animation.targetVisible();
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

    return m_searchQuery.empty() ? m_hitTester.hitTest(*frame, localX, localY) : searchTargetAt(*frame, localX, localY);
}

void OverlayRenderer::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_LAST_MOMENT || !m_animation.renderable())
        return;

    const auto alpha = std::clamp(static_cast<float>(m_animation.value()), 0.0F, 1.0F);

    if (alpha > 0.001F)
        renderCurrentMonitor(alpha);

    if (m_animation.running())
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

    drawRect(box, CHyprColor{0.010F, 0.012F, 0.018F, static_cast<float>(1.00 * backdropAlpha)}, damage, 0, true);
    drawRect(box, CHyprColor{0.002F, 0.003F, 0.006F, static_cast<float>(0.86 * backdropAlpha)}, damage);

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
            m_frameBoundsByMonitor[snapshot.id] = globalBoundsForMonitor(monitor, renderSize);
            m_frames.push_back(m_layout.compute(m_state, snapshot, renderSize, layoutOptionsFor(m_config.layoutMode())));
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
            m_frames.push_back(m_layout.compute(m_state, monitor, renderSize, layoutOptionsFor(m_config.layoutMode())));
        }
    }

    if (m_selectedFrameMonitorId != -1 && !frameForMonitor(m_selectedFrameMonitorId))
        m_selectedFrameMonitorId = -1;

    rebuildSearchMatches();
}

void OverlayRenderer::rebuildSearchMatches() {
    m_searchMatches.clear();
    if (m_searchQuery.empty())
        return;

    for (const auto& frame : m_frames) {
        const auto matches = m_searchMatcher.matchingWindowIds(frame, m_searchQuery);
        m_searchMatches.insert(matches.begin(), matches.end());
    }
}

void OverlayRenderer::selectFirstSearchMatch() {
    if (m_searchQuery.empty())
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
    if (m_searchQuery.empty())
        return targets;

    std::unordered_set<std::int64_t> workspaceIds;
    std::unordered_set<std::uint64_t> windowIds;
    for (const auto& frame : m_frames) {
        for (const auto& workspace : frame.workspaces) {
            const auto workspaceNumber = std::to_string(workspace.workspaceId);
            if (!workspaceIds.contains(workspace.workspaceId) &&
                (m_searchMatcher.matches(workspace.name, m_searchQuery) || m_searchMatcher.matches(workspaceNumber, m_searchQuery))) {
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
}

void OverlayRenderer::renderFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    const auto mode          = m_config.layoutMode();
    const auto searchActive  = !m_searchQuery.empty();
    const auto contentAlpha  = searchActive ? alpha * 0.055 : alpha;
    const auto cardFill      = CHyprColor{0.075F, 0.081F, 0.102F, static_cast<float>(0.985 * contentAlpha)};
    const auto activeFill    = CHyprColor{0.092F, 0.101F, 0.128F, static_cast<float>(0.99 * contentAlpha)};
    const auto selectedFill  = CHyprColor{0.105F, 0.112F, 0.128F, static_cast<float>(0.99 * contentAlpha)};
    const auto emptyFill     = CHyprColor{0.035F, 0.039F, 0.052F, static_cast<float>(0.82 * contentAlpha)};
    const auto windowFill    = CHyprColor{0.102F, 0.110F, 0.136F, static_cast<float>(0.985 * contentAlpha)};
    const auto windowSelect  = CHyprColor{0.118F, 0.125F, 0.142F, static_cast<float>(0.99 * contentAlpha)};
    const auto accent        = CHyprColor{0.42F, 0.88F, 0.82F, static_cast<float>(0.96 * contentAlpha)};
    const auto selectAccent  = CHyprColor{0.98F, 0.74F, 0.28F, static_cast<float>(0.96 * contentAlpha)};
    const auto shadow        = CHyprColor{0.0F, 0.0F, 0.0F, static_cast<float>(0.50 * contentAlpha)};
    const auto selectedGlow  = CHyprColor{0.98F, 0.74F, 0.28F, static_cast<float>(0.11 * contentAlpha)};

    double contentLeft  = frame.bounds.width;
    double contentRight = 0.0;
    double contentTop   = frame.bounds.height;
    double contentBottom = 0.0;
    bool   hasContent   = false;
    for (const auto& workspace : frame.workspaces) {
        if (workspace.rect.width <= 0.0 || workspace.rect.height <= 0.0)
            continue;
        contentLeft  = std::min(contentLeft, workspace.rect.x);
        contentRight = std::max(contentRight, workspace.rect.x + workspace.rect.width);
        contentTop   = std::min(contentTop, workspace.rect.y);
        contentBottom = std::max(contentBottom, workspace.rect.y + workspace.rect.height);
        hasContent   = true;
    }

    const auto titleX = hasContent ? contentLeft : (mode == LayoutMode::Stage ? 68.0 : 46.0);
    const auto titleY = hasContent ? std::max(34.0, contentTop - 58.0) : 34.0;
    if (hasContent && mode != LayoutMode::Stage) {
        const auto stageBox = CBox{
            std::max(18.0, contentLeft - 44.0),
            std::max(18.0, titleY - 24.0),
            std::min(frame.bounds.width - 36.0, contentRight - contentLeft + 88.0),
            std::min(frame.bounds.height - 36.0, contentBottom - titleY + 62.0),
        };
        drawRect(CBox{stageBox.x + 8.0, stageBox.y + 12.0, stageBox.w, stageBox.h}, shadow, damage, 34);
        drawRect(stageBox, CHyprColor{0.018F, 0.021F, 0.030F, static_cast<float>(0.90 * contentAlpha)}, damage, 32, true);
    }
    renderLabel("Workspace overview", titleX, titleY, std::max(1.0, frame.bounds.width * 0.45), 22, contentAlpha, damage);
    if (!searchActive && hasContent) {
        const auto helpWidth = std::min(430.0, std::max(1.0, contentRight - contentLeft));
        renderLabel("Type to search  |  Arrows navigate  |  Enter open", std::max(contentLeft, contentRight - helpWidth),
            titleY + 4.0, helpWidth, 12, contentAlpha * 0.58, damage);
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
        drawRect(CBox{dockBox.x + 4.0, dockBox.y + 6.0, dockBox.w, dockBox.h}, shadow, damage, 18);
        drawRect(dockBox, CHyprColor{0.025F, 0.028F, 0.038F, static_cast<float>(0.78 * contentAlpha)}, damage, 16);
    }

    for (const auto& workspace : frame.workspaces) {
        const auto workspaceSelected = frame.monitorId == m_selectedFrameMonitorId &&
            sameTarget(m_selectedTarget, {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId});
        const auto workspaceBox = boxFor(workspace.rect);
        const auto compact = workspace.rect.height <= 120.0;
        const auto round   = compact ? 14 : 20;

        drawRect(CBox{workspaceBox.x + 5.0, workspaceBox.y + 7.0, workspaceBox.w, workspaceBox.h}, shadow, damage, round);
        if (workspaceSelected && compact)
            drawRect(CBox{workspaceBox.x - 4.0, workspaceBox.y - 4.0, workspaceBox.w + 8.0, workspaceBox.h + 8.0}, selectedGlow, damage, round + 4);

        const auto fill = workspaceSelected ? selectedFill : (workspace.active ? activeFill : (workspace.empty ? emptyFill : cardFill));
        drawRect(workspaceBox, fill, damage, round);

        if (workspace.active) {
            drawRect(CBox{workspaceBox.x + workspaceBox.w - 31.0, workspaceBox.y + 16.0, 14.0, 14.0},
                CHyprColor{0.39F, 0.82F, 0.78F, static_cast<float>(0.14 * contentAlpha)}, damage, 7);
            drawRect(CBox{workspaceBox.x + workspaceBox.w - 28.0, workspaceBox.y + 19.0, 8.0, 8.0}, accent, damage, 4);
        }

        if (workspaceSelected) {
            const auto accentWidth = std::min(compact ? 28.0 : 58.0, std::max(1.0, workspaceBox.w - 28.0));
            drawRect(CBox{workspaceBox.x + centered(workspaceBox.w, accentWidth), workspaceBox.y + workspaceBox.h - 6.0, accentWidth, 4.0},
                selectAccent, damage, 2);
        }

        renderLabel(workspace.name, workspace.rect.x + (compact ? 14.0 : 20.0), workspace.rect.y + (compact ? 10.0 : 16.0),
            std::max(1.0, workspace.rect.width - (compact ? 28.0 : 48.0)), compact ? 13 : 17, contentAlpha, damage);

        if (workspace.empty && !compact)
            renderLabel("Empty", workspace.rect.x + 20.0, workspace.rect.y + 48.0, std::max(1.0, workspace.rect.width - 40.0), 12, contentAlpha * 0.42, damage);

        for (const auto& window : workspace.windows) {
            const auto windowSelected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
                m_selectedTarget,
                {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
            const auto windowBox = boxFor(window.rect);
            const auto windowRound = compact ? 8 : 12;
            const auto footerHeight = compact ? 0.0 : std::min(38.0, std::max(30.0, windowBox.h * 0.22));
            if (windowSelected)
                drawRect(CBox{windowBox.x - 3.0, windowBox.y - 3.0, windowBox.w + 6.0, windowBox.h + 6.0}, selectedGlow, damage, windowRound + 3);
            drawRect(windowBox, windowSelected ? windowSelect : windowFill, damage, windowRound);
            if (windowSelected) {
                const auto accentHeight = std::min(30.0, std::max(1.0, windowBox.h - 18.0));
                drawRect(CBox{windowBox.x + 1.0, windowBox.y + centered(windowBox.h, accentHeight), 3.0, accentHeight},
                    selectAccent, damage, 2);
            }

            if (!compact && windowBox.h > 88.0) {
                const auto previewShell = CBox{
                    windowBox.x + 10.0,
                    windowBox.y + 10.0,
                    std::max(1.0, windowBox.w - 20.0),
                    std::max(1.0, windowBox.h - footerHeight - 16.0),
                };
                drawRect(previewShell, CHyprColor{0.030F, 0.034F, 0.046F, static_cast<float>(0.86 * contentAlpha)}, damage, 10);
                renderWindowPreview(window, previewShell, contentAlpha, damage);
                drawRect(CBox{windowBox.x, windowBox.y + windowBox.h - footerHeight, windowBox.w, footerHeight},
                    CHyprColor{0.050F, 0.054F, 0.070F, static_cast<float>(0.88 * contentAlpha)}, damage, 10);
                renderLabel(window.label, window.rect.x + 14.0, window.rect.y + window.rect.height - footerHeight + 8.0,
                    std::max(1.0, window.rect.width - 28.0), 11, contentAlpha, damage);
            } else {
                renderLabel(window.label, window.rect.x + (compact ? 8.0 : 14.0), window.rect.y + (compact ? 6.0 : 9.0),
                    std::max(1.0, window.rect.width - (compact ? 16.0 : 24.0)), compact ? 10 : 12, contentAlpha, damage);
            }
        }
    }

    if (searchActive) {
        drawRect(CBox{0.0, 0.0, frame.bounds.width, frame.bounds.height}, CHyprColor{0.0F, 0.0F, 0.0F, static_cast<float>(0.72 * alpha)}, damage);
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
    data.round    = 8;
    data.clipBox  = clipBox;
    data.surface  = surface;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
}

void OverlayRenderer::renderSearchPanel(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    if (m_searchQuery.empty())
        return;

    const auto targets        = matchingSearchTargets();
    const auto geometry       = searchPanelGeometry(frame, targets.size());
    const auto visibleStart   = visibleSearchStart(targets, m_selectedTarget, geometry.capacity);
    const auto visibleEnd     = std::min(targets.size(), visibleStart + geometry.capacity);
    const auto panelFill      = CHyprColor{0.055F, 0.060F, 0.078F, static_cast<float>(0.97 * alpha)};
    const auto inputFill      = CHyprColor{0.105F, 0.112F, 0.140F, static_cast<float>(0.98 * alpha)};
    const auto rowFill        = CHyprColor{0.090F, 0.097F, 0.122F, static_cast<float>(0.82 * alpha)};
    const auto selectedFill   = CHyprColor{0.175F, 0.158F, 0.112F, static_cast<float>(0.98 * alpha)};
    const auto selectedAccent = CHyprColor{0.98F, 0.76F, 0.25F, static_cast<float>(1.00 * alpha)};

    drawRect(CBox{geometry.panel.x + 10.0, geometry.panel.y + 14.0, geometry.panel.w, geometry.panel.h},
        CHyprColor{0.0F, 0.0F, 0.0F, static_cast<float>(0.42 * alpha)}, damage, 28);
    drawRect(geometry.panel, panelFill, damage, 26, true);
    drawRect(geometry.input, inputFill, damage, 16);
    renderLabel("Search open windows and workspaces", geometry.panel.x + 28.0, geometry.panel.y + 20.0,
        geometry.panel.w - 190.0, 16, alpha, damage);
    renderLabel(std::format("{} result{}", targets.size(), targets.size() == 1 ? "" : "s"),
        geometry.panel.x + geometry.panel.w - 132.0, geometry.panel.y + 23.0, 104.0, 11, alpha * 0.52, damage);
    renderLabel(std::format("{}_", m_searchQuery), geometry.input.x + 20.0, geometry.input.y + 17.0,
        std::max(1.0, geometry.input.w - 40.0), 20, alpha, damage);

    for (std::size_t index = visibleStart; index < visibleEnd; ++index) {
        const auto& target   = targets[index];
        const auto selected  = sameTarget(target, m_selectedTarget);
        const auto rowIndex  = index - visibleStart;
        const auto row       = CBox{
            geometry.input.x,
            geometry.resultsY + static_cast<double>(rowIndex) * (geometry.rowHeight + geometry.rowGap),
            geometry.input.w,
            geometry.rowHeight,
        };

        if (selected)
            drawRect(CBox{row.x - 4.0, row.y - 4.0, row.w + 8.0, row.h + 8.0},
                CHyprColor{0.98F, 0.68F, 0.16F, static_cast<float>(0.11 * alpha)}, damage, 16);
        drawRect(row, selected ? selectedFill : rowFill, damage, 13);
        if (selected) {
            drawRect(CBox{row.x, row.y + 10.0, 4.0, row.h - 20.0}, selectedAccent, damage, 2);
        }

        if (target.type == OverviewTargetType::Workspace) {
            const auto* workspace = findWorkspaceCard(target.workspaceId);
            const auto  name      = workspace && !workspace->name.empty() ? workspace->name : std::to_string(target.workspaceId);
            drawRect(CBox{row.x + 18.0, row.y + 17.0, 86.0, 28.0},
                CHyprColor{0.105F, 0.158F, 0.154F, static_cast<float>(0.92 * alpha)}, damage, 10);
            renderLabel("SPACE", row.x + 35.0, row.y + 24.0, 52.0, 9, alpha * 0.72, damage);
            renderLabel(std::format("Workspace {}", name), row.x + 120.0, row.y + 9.0, row.w - 140.0, 14, alpha, damage);
            renderLabel("Switch to this workspace", row.x + 120.0, row.y + 35.0, row.w - 140.0, 10, alpha * 0.50, damage);
        } else if (const auto* window = findWindowCard(target.windowId)) {
            drawRect(CBox{row.x + 18.0, row.y + 17.0, 86.0, 28.0},
                CHyprColor{0.160F, 0.136F, 0.090F, static_cast<float>(0.92 * alpha)}, damage, 10);
            renderLabel("WINDOW", row.x + 30.0, row.y + 24.0, 64.0, 9, alpha * 0.72, damage);
            renderLabel(window->label, row.x + 120.0, row.y + 9.0, row.w - 140.0, 14, alpha, damage);
            renderLabel(std::format("Workspace {}", window->workspaceId), row.x + 120.0, row.y + 35.0, row.w - 140.0, 10, alpha * 0.50, damage);
        }
    }

    if (targets.empty()) {
        renderLabel("No results", geometry.input.x + 4.0, geometry.resultsY + 16.0, geometry.input.w - 8.0, 18, alpha * 0.82, damage);
        renderLabel("Try another window title, workspace name, or number", geometry.input.x + 4.0, geometry.resultsY + 50.0,
            geometry.input.w - 8.0, 12, alpha * 0.48, damage);
    }

    renderLabel("Up/Down select   Enter open   Esc clear", geometry.panel.x + 28.0, geometry.panel.y + geometry.panel.h - 28.0,
        geometry.panel.w - 56.0, 11, alpha * 0.48, damage);
}

OverviewTarget OverlayRenderer::searchTargetAt(const WorkspaceWallFrame& frame, double x, double y) const {
    const auto targets = matchingSearchTargets();
    if (targets.empty())
        return {};

    const auto geometry     = searchPanelGeometry(frame, targets.size());
    const auto visibleStart = visibleSearchStart(targets, m_selectedTarget, geometry.capacity);
    const auto visibleEnd   = std::min(targets.size(), visibleStart + geometry.capacity);

    for (std::size_t index = visibleStart; index < visibleEnd; ++index) {
        const auto rowIndex = index - visibleStart;
        const auto row      = LayoutRect{
            .x      = geometry.input.x,
            .y      = geometry.resultsY + static_cast<double>(rowIndex) * (geometry.rowHeight + geometry.rowGap),
            .width  = geometry.input.w,
            .height = geometry.rowHeight,
        };
        if (contains(row, x, y))
            return targets[index];
    }

    return {};
}

const WindowCard* OverlayRenderer::findWindowCard(std::uint64_t windowId) const noexcept {
    for (const auto& frame : m_frames) {
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
    if (!g_pHyprRenderer || text.empty() || maxWidth <= 0.0 || alpha <= 0.001)
        return;

    const auto key = std::format("{}:{}:{}", pointSize, static_cast<int>(std::ceil(maxWidth)), text);
    auto       it  = m_textures.find(key);
    if (it == m_textures.end()) {
        it = m_textures.emplace(key, g_pHyprRenderer->renderText(text, CHyprColor{0.92F, 0.95F, 1.0F, 1.0F}, pointSize, false, "", static_cast<int>(maxWidth))).first;
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

        localX = x - bounds->second.x;
        localY = y - bounds->second.y;
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
