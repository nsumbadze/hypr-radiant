#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

namespace hypr_radiant {
namespace {

bool sameTarget(OverviewTarget lhs, OverviewTarget rhs) {
    return lhs.type == rhs.type && lhs.workspaceId == rhs.workspaceId && lhs.windowId == rhs.windowId;
}

CBox boxFor(const LayoutRect& rect) {
    return CBox{std::round(rect.x), std::round(rect.y), std::round(rect.width), std::round(rect.height)};
}

void drawRect(const CBox& box, CHyprColor color, const CRegion& damage, int round = 0) {
    if (!g_pHyprRenderer || box.w <= 0.0 || box.h <= 0.0)
        return;

    (void)damage;

    CRectPassElement::SRectData data;
    data.box   = box;
    data.color = color;
    data.round = round;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
}

void drawBorder(const CBox& box, double size, CHyprColor color, const CRegion& damage) {
    if (box.w <= 0.0 || box.h <= 0.0 || size <= 0.0)
        return;

    const auto border = std::min({size, box.w / 2.0, box.h / 2.0});
    drawRect(CBox{box.x, box.y, box.w, border}, color, damage);
    drawRect(CBox{box.x, box.y + box.h - border, box.w, border}, color, damage);
    drawRect(CBox{box.x, box.y, border, box.h}, color, damage);
    drawRect(CBox{box.x + box.w - border, box.y, border, box.h}, color, damage);
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
        .outerPadding          = 150.0,
        .cardGap               = 34.0,
        .windowGap             = 14.0,
        .windowInset           = 30.0,
    };
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

    const auto target = m_hitTester.hitTest(*frame, localX, localY);
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
    damageAllMonitors();
}

void OverlayRenderer::clearSearchOrHide() {
    if (!m_searchQuery.empty()) {
        clearSearch();
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

    return m_hitTester.hitTest(*frame, localX, localY);
}

void OverlayRenderer::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_LAST_MOMENT || !m_animation.renderable())
        return;

    const auto alpha = std::clamp(static_cast<float>(m_animation.value()) * m_config.opacity(), 0.0F, 1.0F);

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

    drawRect(box, CHyprColor{0.0F, 0.0F, 0.0F, static_cast<float>(alpha)}, damage);

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

    if (const auto* frame = frameForSelectedTarget()) {
        if (const auto target = m_searchMatcher.firstMatch(*frame, m_searchQuery)) {
            m_selectedTarget = *target;
            m_selectedFrameMonitorId = frame->monitorId;
            return;
        }
    }

    for (const auto& frame : m_frames) {
        if (const auto target = m_searchMatcher.firstMatch(frame, m_searchQuery)) {
            m_selectedTarget = *target;
            m_selectedFrameMonitorId = frame.monitorId;
            return;
        }
    }

    m_selectedTarget = {};
}

void OverlayRenderer::clearSearch() {
    m_searchQuery.clear();
    m_searchMatches.clear();
}

void OverlayRenderer::renderFrame(const WorkspaceWallFrame& frame, double alpha, const CRegion& damage) {
    const auto mode                = m_config.layoutMode();
    const auto cardFill            = CHyprColor{0.035F, 0.16F, 0.13F, static_cast<float>(0.88 * alpha)};
    const auto cardSelectedFill    = CHyprColor{0.08F, 0.28F, 0.22F, static_cast<float>(0.96 * alpha)};
    const auto emptyFill           = CHyprColor{0.012F, 0.055F, 0.045F, static_cast<float>(0.48 * alpha)};
    const auto windowFill          = CHyprColor{0.075F, 0.24F, 0.19F, static_cast<float>(0.86 * alpha)};
    const auto windowSelectedFill  = CHyprColor{0.13F, 0.37F, 0.29F, static_cast<float>(0.98 * alpha)};
    const auto windowTitleFill     = CHyprColor{0.02F, 0.09F, 0.075F, static_cast<float>(0.78 * alpha)};
    const auto border              = CHyprColor{0.16F, 0.70F, 0.58F, static_cast<float>(0.58 * alpha)};
    const auto activeAccent        = CHyprColor{0.20F, 0.95F, 0.78F, static_cast<float>(0.95 * alpha)};
    const auto selectAccent        = CHyprColor{0.98F, 0.84F, 0.25F, static_cast<float>(1.0 * alpha)};
    const auto selectedInnerAccent = CHyprColor{0.45F, 1.0F, 0.78F, static_cast<float>(0.75 * alpha)};
    const auto windowBorder        = CHyprColor{0.32F, 0.86F, 0.70F, static_cast<float>(0.36 * alpha)};
    const auto windowDimFill       = CHyprColor{0.018F, 0.07F, 0.06F, static_cast<float>(0.50 * alpha)};
    const auto windowDimBorder     = CHyprColor{0.10F, 0.34F, 0.28F, static_cast<float>(0.24 * alpha)};
    const auto stageFill           = CHyprColor{0.015F, 0.045F, 0.038F, static_cast<float>(0.82 * alpha)};
    const auto stageBorder         = CHyprColor{0.20F, 0.76F, 0.60F, static_cast<float>(0.48 * alpha)};
    const auto headerFill          = CHyprColor{0.03F, 0.12F, 0.10F, static_cast<float>(0.88 * alpha)};

    const auto searchActive = !m_searchQuery.empty();
    const auto header       = searchActive ? std::format("Workspace Overview  Search: {}", m_searchQuery) : std::string{"Workspace Overview"};

    if (mode == LayoutMode::Stage) {
        const auto stageBox  = CBox{28.0, 54.0, std::max(1.0, frame.bounds.width - 56.0), std::max(1.0, frame.bounds.height - 82.0)};
        const auto headerBox = CBox{46.0, 36.0, std::min(760.0, frame.bounds.width - 92.0), 44.0};
        drawRect(stageBox, stageFill, damage, 24);
        drawBorder(stageBox, 2.0, stageBorder, damage);
        drawRect(headerBox, headerFill, damage, 16);
        drawBorder(headerBox, 1.5, stageBorder, damage);
        renderLabel(header, headerBox.x + 18.0, headerBox.y + 10.0, std::max(1.0, headerBox.w - 36.0), 19, alpha, damage);
    } else {
        renderLabel(header, frame.bounds.x + 46.0, frame.bounds.y + 42.0, std::max(1.0, frame.bounds.width - 92.0), 22, alpha, damage);
    }

    for (const auto& workspace : frame.workspaces) {
        const auto workspaceSelected = frame.monitorId == m_selectedFrameMonitorId &&
            sameTarget(m_selectedTarget, {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId});
        const auto workspaceBox = boxFor(workspace.rect);
        drawRect(workspaceBox, workspaceSelected ? cardSelectedFill : (workspace.empty ? emptyFill : cardFill), damage, mode == LayoutMode::Stage ? 22 : 18);
        drawBorder(workspaceBox, mode == LayoutMode::Stage ? 2.0 : 1.5, border, damage);

        if (workspace.active) {
            drawRect(CBox{workspaceBox.x, workspaceBox.y, workspaceBox.w, 6.0}, activeAccent, damage);
            drawBorder(workspaceBox, 3.0, activeAccent, damage);
        }

        if (workspaceSelected) {
            drawRect(CBox{workspaceBox.x + 8.0, workspaceBox.y + 8.0, workspaceBox.w - 16.0, 3.0}, selectedInnerAccent, damage);
            drawBorder(workspaceBox, mode == LayoutMode::Stage ? 7.0 : 6.0, selectAccent, damage);
        }

        renderLabel(workspace.name, workspace.rect.x + 20.0, workspace.rect.y + 16.0, std::max(1.0, workspace.rect.width - 40.0), 16, alpha, damage);

        for (const auto& window : workspace.windows) {
            const auto windowSelected = frame.monitorId == m_selectedFrameMonitorId && sameTarget(
                m_selectedTarget,
                {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId});
            const auto windowMatchesSearch = !searchActive || m_searchMatches.contains(window.stableId);
            const auto windowBox = boxFor(window.rect);
            drawRect(windowBox, windowMatchesSearch ? (windowSelected ? windowSelectedFill : windowFill) : windowDimFill, damage, 10);
            drawBorder(windowBox, 1.0, windowMatchesSearch ? windowBorder : windowDimBorder, damage);

            if (windowSelected) {
                drawRect(CBox{windowBox.x, windowBox.y, windowBox.w, std::min(30.0, windowBox.h)}, windowTitleFill, damage, 10);
                drawRect(CBox{windowBox.x, windowBox.y, windowBox.w, 4.0}, selectAccent, damage);
                drawBorder(windowBox, 5.0, selectAccent, damage);
            }

            renderLabel(window.label, window.rect.x + 12.0, window.rect.y + 10.0, std::max(1.0, window.rect.width - 24.0), 11, windowMatchesSearch ? alpha : alpha * 0.38, damage);
        }
    }
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
