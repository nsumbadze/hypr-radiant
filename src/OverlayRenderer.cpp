#include <hypr-radiant/OverlayRenderer.hpp>
#include <hypr-radiant/Log.hpp>
#include <hypr-radiant/SearchPanelGeometry.hpp>
#include <hypr-radiant/Theme.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
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
        .minimumWorkspaceSlots = 0,
        .outerPadding          = 48.0,
        .cardGap               = 20.0,
        .windowGap             = 8.0,
        .windowInset           = 16.0,
        .focusedStage          = true,
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

    auto backdrop = Theme::backdropColor();
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
    const auto mode         = m_config.layoutMode();
    const auto searchActive = !m_searchQuery.empty();
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
    if (m_searchQuery.empty())
        return;

    const auto targets        = matchingSearchTargets();
    const auto geometry       = computeSearchPanelGeometry(frame, targets.size());
    const auto visibleStart   = visibleSearchStart(targets, m_selectedTarget, geometry.capacity);
    const auto visibleEnd     = std::min(targets.size(), visibleStart + geometry.capacity);

    const auto withAlpha = [](CHyprColor color, double multiplier) {
        color.a *= multiplier;
        return color;
    };

    const auto panelBox = CBox{geometry.panelX, geometry.panelY, geometry.panelW, geometry.panelH};
    const auto inputBox = CBox{geometry.inputX, geometry.inputY, geometry.inputW, geometry.inputH};

    drawRect(CBox{panelBox.x + Theme::shadowOffsetX(), panelBox.y + Theme::shadowOffsetY(), panelBox.w, panelBox.h},
        withAlpha(Theme::shadowColor(), alpha), damage, Theme::searchRadius());
    drawRect(panelBox, withAlpha(Theme::searchPanelColor(), alpha), damage, Theme::searchRadius(), true);

    CBorderPassElement::SBorderData border;
    border.box        = panelBox;
    const auto panelBorder = Theme::cardBorder(false, false, static_cast<float>(alpha));
    border.grad1      = Config::CGradientValueData{panelBorder};
    border.a          = static_cast<float>(panelBorder.a);
    border.round      = Theme::searchRadius();
    border.borderSize = 1;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(border));

    drawRect(inputBox, withAlpha(Theme::searchInputColor(), alpha), damage, Theme::inputRadius());
    renderLabel(std::format("{}_", m_searchQuery), inputBox.x + 16.0, inputBox.y + 13.0,
        std::max(1.0, inputBox.w - 80.0), Theme::titleSize(), alpha, damage);
    renderLabel(std::format("{} result{}", targets.size(), targets.size() == 1 ? "" : "s"),
        inputBox.x + inputBox.w - 60.0, inputBox.y + 17.0, 48.0, Theme::hintSize(), alpha * 0.50, damage);

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

        drawRect(row, Theme::searchRowFill(selected, static_cast<float>(alpha)), damage, Theme::inputRadius());
        if (selected) {
            const auto accentHeight = std::max(1.0, row.h - 20.0);
            drawRect(CBox{row.x, row.y + (row.h - accentHeight) / 2.0, 4.0, accentHeight},
                withAlpha(Theme::searchRowAccent(), alpha), damage, 2);
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

    // m_textures is an open-session text cache keyed by (pointSize, ceil(maxWidth), text).
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
    const auto key = std::format("{}:{}:{}", pointSize, static_cast<int>(std::ceil(maxWidth)), text);
    auto       it  = m_textures.find(key);
    if (it == m_textures.end()) {
        it = m_textures.emplace(key, g_pHyprRenderer->renderText(text, Theme::textColor(), pointSize, false, "", static_cast<int>(maxWidth))).first;
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
